# DSA 07.06: Paths, Ancestors and LCA in a Binary Tree

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (3 reading, 6 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md), [Bottom-up recursion](04_bottom_up_recursion.md) (top-down state, the combine pattern) and [Level, view and width problems](05_level_view_and_width_problems.md) (the level-by-level loop) · Related: the intersection of two linked lists in [Core patterns: two pointers and reversal](../04_linked_lists/02_core_patterns_two_pointers_reversal.md), and prefix sums in [Prefix sums and difference arrays](../01_arrays_and_strings/04_prefix_sums_and_difference_arrays.md) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- answer the root-to-leaf questions (does a path with this sum exist, list all of them) with one shared path vector;
- count the downward paths with a given sum anywhere in the tree in `O(n)` with prefix sums, and explain the undo step that makes it correct;
- find a lowest common ancestor in three ways (bottom-up recursion, a version that survives a missing node, and parent links), and get the distance between two nodes from it;
- list the nodes at distance `K` from a node and compute a burn time by treating the tree as a graph;
- flatten a tree into a preorder chain in place with `O(1)` extra space, and count the nodes of a complete tree in `O(log^2 n)`.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 3,006,687 checks passed under AddressSanitizer with UBSan (GCC 13.3). Each function was compared with a separately written reference on every tree shape of 0 to 8 nodes (two random labellings each, with negative values, zeros and repeats), on random trees of 15, 30 and 60 nodes, and, for the pair functions, on every pair of nodes of a tree (the first 40 nodes of larger ones). The references climb parent links, enumerate every ancestor-descendant pair, or run a breadth-first search from each node. The non-recursive code also ran on a chain of one million nodes. The traces come from a separate short Python model. The visit counts are exact. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`; they are noisy and only show orders of magnitude. LeetCode statements are summarised from memory; titles were checked (see Sources), and no difficulty labels are given because none could be verified.

All listings need `<algorithm>`, `<initializer_list>`, `<unordered_map>`, `<unordered_set>`, `<utility>` and `<vector>`. The node is the plain one:

```cpp
struct Node {
    int val;
    Node* left = nullptr;
    Node* right = nullptr;
};
```

Two example trees are used throughout. T is the tree of the earlier tutorials, and S is the usual example for path sums:

```
          1                         5
        /   \                     /   \
       2     6                   4     8
      / \     \                 /     / \
     3   4     7               11    13  4
        /                     /  \      / \
       5                     7    2    5   1
```

---

## 1. Paths that start at the root

### 1.1 Root to leaf

A **root-to-leaf path** is the sequence of nodes from the root down to a leaf. The question "is there a path with sum `target`" (LeetCode 112) is a top-down recursion: subtract the node's value from what is still needed and pass the rest down. Two details decide whether it is right. The path must end at a **leaf**, so the test `remaining == 0` happens only at a leaf; and values may be negative, so you cannot stop early when the remaining amount goes below zero.

```cpp
inline bool hasPathSum(const Node* n, long long target) {              // LeetCode 112
    if (!n) return false;
    target -= n->val;                                                  // what is still needed below
    if (!n->left && !n->right) return target == 0;                     // the path must end at a leaf
    return hasPathSum(n->left, target) || hasPathSum(n->right, target);
}

// One shared vector holds the current path: push on the way down, pop on the way up.
inline void collectPaths(const Node* n, long long remaining, bool useTarget, std::vector<int>& path,
                         std::vector<std::vector<int>>& out) {
    if (!n) return;
    path.push_back(n->val);
    remaining -= n->val;
    if (!n->left && !n->right) {
        if (!useTarget || remaining == 0) out.push_back(path);         // copy only at a leaf that qualifies
    } else {
        collectPaths(n->left, remaining, useTarget, path, out);
        collectPaths(n->right, remaining, useTarget, path, out);
    }
    path.pop_back();                                                   // undo the push
}

inline std::vector<std::vector<int>> allPaths(const Node* root) {      // LeetCode 257
    std::vector<std::vector<int>> out;
    std::vector<int> path;
    collectPaths(root, 0, false, path, out);
    return out;
}

inline std::vector<std::vector<int>> pathSumII(const Node* root, long long target) {   // LeetCode 113
    std::vector<std::vector<int>> out;
    std::vector<int> path;
    collectPaths(root, target, true, path, out);
    return out;
}
```

To list the paths (LeetCode 257, and 113 with a target) one vector holds the current path. Push the node on the way down and pop it on the way up, so the same storage is reused by every path, and **copy only at a leaf** (the copy is the output, and the cost is proportional to the output). On S with target 22 there are two such paths, 5, 4, 11, 2 and 5, 8, 4, 5; the leaf sums of S are 27, 22, 26, 22 and 18. The target 5 gives false: the root is not a leaf, and a single-child tree `1 (2)` has no path with sum 1.

Cost: `O(n)` time to walk, plus `O(h)` per reported path to copy it. Space: `O(h)` for the vector and the stack.

### 1.2 Any downward path: prefix sums on a tree

LeetCode 437 asks for the number of **downward paths** (from any node to a descendant, not necessarily ending at a leaf) with a given sum. The direct way starts a walk at every node and counts the matches below it:

```cpp
// LeetCode 437: paths that go downward from any node to any descendant, and add up to target.
inline long long fromHere(const Node* n, long long remaining) {        // paths that START at n
    if (!n) return 0;
    remaining -= n->val;
    return (remaining == 0 ? 1 : 0) + fromHere(n->left, remaining) + fromHere(n->right, remaining);
}

inline long long countPathsNaive(const Node* root, long long target) { // O(n * h)
    if (!root) return 0;
    return fromHere(root, target) + countPathsNaive(root->left, target) + countPathsNaive(root->right, target);
}

// Prefix sums on the current root-to-node path: a path (a, b] adds up to target when
// prefix(b) - prefix(a) == target, so count the earlier prefixes equal to prefix(b) - target.
inline long long prefixWalk(const Node* n, long long prefix, long long target,
                            std::unordered_map<long long, int>& seen) {
    if (!n) return 0;
    prefix += n->val;
    auto it = seen.find(prefix - target);
    long long found = it == seen.end() ? 0 : it->second;               // paths ending at n
    ++seen[prefix];                                                    // n joins the current path
    found += prefixWalk(n->left, prefix, target, seen);
    found += prefixWalk(n->right, prefix, target, seen);
    --seen[prefix];                                                    // n leaves it: siblings must not see it
    return found;
}

inline long long countPaths(const Node* root, long long target) {
    std::unordered_map<long long, int> seen;
    seen[0] = 1;                                                       // the empty prefix, for paths from the root
    return prefixWalk(root, 0, target, seen);
}
```

The direct version visits each node once per ancestor, so it is `O(n h)`. Counting calls on a non-null node (target 0):

| Tree | `n` | Direct | Prefix sums |
|---|---|---|---|
| Chain, all values 1 | 1000 | 501,500 | 1000 |
| Random shape | 1000 | 13,633 | 1000 |
| Perfect | 1023 | 10,240 | 1023 |

The chain is `n + n(n + 1)/2`, and the perfect tree `n + (10 * 1024 - 1023)` (the sum of the subtree sizes). The prefix version is `O(n)`: one visit per node. The idea is the one from the prefix-sums tutorial. On the path from the root to the current node, let `prefix(x)` be the sum of the values from the root to `x`. A downward path from just below `a` down to `b` has sum `prefix(b) - prefix(a)`. So when you arrive at `b`, the number of paths ending at `b` with sum `target` is the number of ancestors `a` (counting an imaginary one above the root, with prefix 0) whose prefix equals `prefix(b) - target`. A hash map from prefix to count, holding exactly the prefixes on the current path, answers that in `O(1)`.

The map must describe the **current path only**. After finishing a node's subtrees, the code removes its prefix again (`--seen[prefix]`). Without that line, a prefix from the left subtree stays visible to the right subtree, where it is not an ancestor, and the count is too large.

Trace on `10 (5 (3 (3, -2), 2 (_, 1)), -3 (_, 11))` with target 8, in visiting order. The lookup key is `prefix - target`:

| Node | Prefix | Key | Paths ending here |
|---|---|---|---|
| 10 | 10 | 2 | 0 |
| 5 | 15 | 7 | 0 |
| 3 | 18 | 10 | 1 (the path 5, 3) |
| 3 | 21 | 13 | 0 |
| -2 | 16 | 8 | 0 |
| 2 | 17 | 9 | 0 |
| 1 | 18 | 10 | 1 (the path 5, 2, 1) |
| -3 | 7 | -1 | 0 |
| 11 | 18 | 10 | 1 (the path -3, 11) |

The total is 3. The prefix 18 appears three times on different paths, which is why the undo matters: when the walk reaches the node 1, the first 18 (from the node 3) has been removed. The map starts as `{0: 1}`, so paths that begin at the root are found. Prefixes and the count use 64 bits: on a chain of 100 nodes of value 0 with target 0 there are 5050 paths, and values near the int limits overflow 32-bit sums (tested with three nodes of 2,000,000,000).

Time, best of 5 (ms), target 0:

| Tree | Direct | Prefix sums |
|---|---|---|
| Perfect, 1,048,575 nodes | 47.1 | 13.4 |
| Random, 1,000,000 nodes | 597.1 | 111.6 |
| Chain, 20,000 nodes, all zeros | 693.5 | 0.3 |

---

## 2. Lowest common ancestor

The **lowest common ancestor** (LCA) of two nodes `p` and `q` is the deepest node that has both of them in its subtree; a node counts as a descendant of itself, so if `p` is an ancestor of `q` the answer is `p`. In T, the LCA of 3 and 5 is 2, of 2 and 5 is 2, and of 5 and 7 is 1.

### 2.1 Bottom-up recursion

```cpp
// Lowest common ancestor, bottom-up. Both p and q must be in the tree.
inline const Node* lca(const Node* n, const Node* p, const Node* q) {  // LeetCode 236
    if (!n || n == p || n == q) return n;                              // found one of them (or ran out)
    const Node* l = lca(n->left, p, q);
    const Node* r = lca(n->right, p, q);
    if (l && r) return n;                                              // one on each side: n is the split point
    return l ? l : r;                                                  // both on one side, or only one exists
}

// The same, but safe when p or q may be missing: count how many of the two each subtree holds.
struct LcaInfo {
    const Node* node;                                                  // the LCA, once both were found
    int found;                                                         // how many of p, q are in this subtree
};
inline LcaInfo lcaChecked(const Node* n, const Node* p, const Node* q) {
    if (!n) return {nullptr, 0};
    LcaInfo l = lcaChecked(n->left, p, q);
    if (l.node) return l;                                              // already decided below
    LcaInfo r = lcaChecked(n->right, p, q);
    if (r.node) return r;
    int found = l.found + r.found + (n == p) + (n == q);
    return {found == 2 ? n : nullptr, found};                          // the first node that sees both is the LCA
}
```

The function returns "what I found in my subtree". A subtree that contains neither node returns null, one that contains one of them returns that node, and the first node that receives a non-null answer from **both** children is the split point, so it returns itself. If only one child answers, that answer is passed upward untouched. The recursion stops at `p` or `q` without looking below them: if `q` is below `p`, finding `p` is enough, because then `p` is the answer. Number of visits on a perfect tree of 1023 nodes:

| Query | Visits |
|---|---|
| Children of the root | 3 |
| The root and any node | 1 |
| Two sibling leaves | 1023 |
| Leftmost and rightmost leaf | 1023 |

The cost is the whole tree, minus the subtrees below `p` and `q`.

### 2.2 When a node may be missing

The classic function **assumes both nodes exist**. If `q` is not in the tree, it returns `p`, a wrong answer that looks like a right one (the test suite has a check for exactly this). To be safe, count how many of the two each subtree holds and accept an answer only when the count reaches 2: the second function does this, returning the node once `found == 2`. It also decides correctly when `p == q` (the node sees both at once) and when one is an ancestor of the other.

### 2.3 With parent links

When there are many queries, or the tree is deep, store the parents. The function below builds the map with an explicit stack, so it is safe on a chain of a million nodes, and finds the LCA by the trick from the linked-list intersection problem: two walkers climb toward the root, and a walker that falls off the top restarts at the other node.

```cpp
using Parents = std::unordered_map<const Node*, const Node*>;

inline Parents buildParents(const Node* root) {                        // iterative: safe on deep trees
    Parents parent;
    std::vector<const Node*> st;
    if (root) { parent[root] = nullptr; st.push_back(root); }
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        for (const Node* c : {n->left, n->right})
            if (c) { parent[c] = n; st.push_back(c); }
    }
    return parent;
}

// Two walkers climb to the root; when one falls off, it restarts at the other's start.
// Both walk the same total distance, so they meet at the LCA after at most depth(p) + depth(q) - depth(lca) + 2
// iterations: the linked-list intersection trick.
inline const Node* lcaByParents(const Parents& parent, const Node* p, const Node* q) {
    const Node* a = p;
    const Node* b = q;
    while (a != b) {
        a = a ? parent.at(a) : q;
        b = b ? parent.at(b) : p;
    }
    return a;                                                          // the LCA (null if the nodes are in different trees)
}

inline int depthOf(const Parents& parent, const Node* n) {
    int d = 0;
    while (parent.at(n)) { n = parent.at(n); ++d; }
    return d;
}

inline int distanceBetween(const Parents& parent, const Node* p, const Node* q) {
    const Node* a = lcaByParents(parent, p, q);
    return depthOf(parent, p) + depthOf(parent, q) - 2 * depthOf(parent, a);
}
```

Why it works: the walker that started at `p` travels `p` up to the root and then `q` up to the LCA, the other travels `q` up and then `p` up to the LCA. Both distances are equal, so they arrive at the LCA in the same iteration. Trace for `p = 5`, `q = 7` in T (the pair is the pair `(a, b)` after each iteration):

| Iteration | `a` | `b` |
|---|---|---|
| 1 | 4 | 6 |
| 2 | 2 | 1 |
| 3 | 1 | null |
| 4 | null | 5 |
| 5 | 7 | 4 |
| 6 | 6 | 2 |
| 7 | 1 | 1 |

They meet at 1, after `depth(p) + depth(q) - depth(lca) + 2 = 3 + 2 - 0 + 2 = 7` iterations (the 2 counts the step off the top of the tree and the jump to the other node). When `p` and `q` have the same depth they meet without any restart. The distance between two nodes follows from the same quantities, `depth(p) + depth(q) - 2 depth(lca)`: for 5 and 7 it is 3 + 2 - 0 = 5, and for 3 and 7 it is 4. Each query costs `O(h)`, and the map costs once: 155.2 ms for the perfect tree of 1,048,575 nodes and 543.0 ms for the random tree of 1,000,000 (an `unordered_map` of that size is slow; storing a parent pointer in the node is much cheaper when you can change the node).

---

## 3. Distance `K` and burning: the tree as a graph

A tree is a graph in which every node also has an edge to its parent. Questions about distance in any direction (not only downward) become breadth-first search on that graph. The only work is to add the missing edge, the parent. Visited nodes are tracked (a set, here), so the search never goes back where it came from.

```cpp
// Breadth-first search from a start node, where the neighbours are left, right AND parent.
inline std::vector<int> nodesAtDistance(const Node* root, const Node* start, int k) {   // LeetCode 863
    Parents parent = buildParents(root);
    std::unordered_set<const Node*> visited{start};
    std::vector<const Node*> cur{start}, nxt;
    for (int d = 0; d < k && !cur.empty(); ++d) {
        nxt.clear();
        for (const Node* n : cur)
            for (const Node* c : std::initializer_list<const Node*>{n->left, n->right, parent.at(n)})
                if (c && visited.insert(c).second) nxt.push_back(c);
        std::swap(cur, nxt);
    }
    std::vector<int> out;
    for (const Node* n : cur) out.push_back(n->val);
    return out;
}

// Minutes until everything burns when the fire starts at `start` and spreads to neighbours each minute.
inline int burnTime(const Node* root, const Node* start) {             // LeetCode 2385
    Parents parent = buildParents(root);
    std::unordered_set<const Node*> visited{start};
    std::vector<const Node*> cur{start}, nxt;
    int minutes = -1;
    while (!cur.empty()) {
        ++minutes;
        nxt.clear();
        for (const Node* n : cur)
            for (const Node* c : std::initializer_list<const Node*>{n->left, n->right, parent.at(n)})
                if (c && visited.insert(c).second) nxt.push_back(c);
        std::swap(cur, nxt);
    }
    return minutes;
}
```

`nodesAtDistance` (LeetCode 863) returns the frontier after `k` rounds. On T from node 4: distance 1 is {2, 5}, distance 2 is {1, 3} and the distances 3 and 4 are {6} and {7}. `burnTime` (LeetCode 2385) is the same search run until the frontier is empty, and returns the last round, the greatest distance from the start. From node 4 it is 4 minutes; from node 7 it is 5 (the farthest node is 5). The search visits each node once, `O(n)`, and uses `O(n)` memory for the map and the set. Because it is breadth-first it needs no recursion and handles deep trees: on a chain of one million nodes both functions gave the right answers.

---

## 4. Flatten, and counting a complete tree

### 4.1 Flatten in place

LeetCode 114 asks to turn the tree into a chain of right pointers in preorder (all left pointers null), in place. The `O(1)`-space version works from the top. For each node with a left subtree, find the last node of that subtree on its right edge (`pred`), hang the node's old right subtree under `pred`, and move the left subtree to the right.

```cpp
// LeetCode 114: make the tree a right-leaning chain in preorder, with O(1) extra space.
inline void flatten(Node* root) {
    for (Node* cur = root; cur; cur = cur->right) {
        if (!cur->left) continue;
        Node* pred = cur->left;
        while (pred->right) pred = pred->right;                        // the last node of the left subtree in preorder
        pred->right = cur->right;                                      // the old right subtree follows it
        cur->right = cur->left;                                        // the left subtree becomes the right one
        cur->left = nullptr;
    }
}

// Recursive version: visit right, then left, then the node, linking each node to the previous one.
inline void flattenRec(Node* n, Node*& prev) {
    if (!n) return;
    flattenRec(n->right, prev);
    flattenRec(n->left, prev);
    n->right = prev;
    n->left = nullptr;
    prev = n;
}
```

Trace on T (each row is a node with a left child, in the order they are reached):

| `cur` | `pred` | Effect |
|---|---|---|
| 1 | 4 | 4.right = 6; 1.right = 2 |
| 2 | 3 | 3.right = 4; 2.right = 3 |
| 4 | 5 | 5.right = 6; 4.right = 5 |

The result is 1, 2, 3, 4, 5, 6, 7. Note that `pred` follows right pointers only, so for the node 1 it is 4, not 5, the last node in preorder. That is enough: when `cur` later reaches 4, the left child 5 is moved and the link from 4 to 6 is carried to 5, so the chain is repaired as `cur` advances. The second function is the recursive form: process the right subtree, then the left, then the node, pointing each node at the previously processed one (`prev`), so the chain is built from the end. It uses `O(h)` stack. Measured on a copy of each tree (best of 3): 7.6 ms against 12.3 ms on the perfect tree, and 13.5 ms against 15.6 ms on the random one. The loop version also ran on a chain of one million nodes.

### 4.2 Counting the nodes of a complete tree

A **complete** tree has every level full except possibly the last, which is filled from the left. Counting the nodes (LeetCode 222) takes `O(n)` if you visit them. The structure allows much better: measure the height of the leftmost path and of the rightmost path from a node. If they are equal, the subtree is **perfect** and has `2^h - 1` nodes with no further looking. If not, then in a complete tree at least one child's subtree is perfect (the last level ends either inside the left or inside the right subtree), so the recursion continues into both children, and one of them stops at once.

```cpp
// LeetCode 222, for a COMPLETE tree: every level full except possibly the last, filled from the left.
inline int leftHeight(const Node* n) { int h = 0; for (; n; n = n->left) ++h; return h; }
inline int rightHeight(const Node* n) { int h = 0; for (; n; n = n->right) ++h; return h; }

inline long long countComplete(const Node* n) {
    if (!n) return 0;
    int lh = leftHeight(n), rh = rightHeight(n);
    if (lh == rh) return (1LL << lh) - 1;                              // the subtree is perfect: no need to look inside
    return 1 + countComplete(n->left) + countComplete(n->right);       // one of the two children is perfect
}
```

Each call walks two paths of length `O(log n)`, and there is a chain of at most `O(log n)` calls that do not stop, so the total is `O(log^2 n)`. Measured: the calls and the link steps (nodes passed while measuring heights):

| `n` | Nodes counted | Calls | Link steps |
|---|---|---|---|
| 1023 (perfect) | 1023 | 1 | 20 |
| 1024 | 1024 | 20 | 212 |
| 524,289 | 524,289 | 37 | 742 |
| 1,000,000 | 1,000,000 | 38 | 755 |
| 1,048,575 (perfect) | 1,048,575 | 1 | 40 |

Visiting every node of the tree of 1,000,000 nodes took 2.03 ms; the logarithmic count took less than 0.001 ms (the timer printed 0.000). The method needs the tree to be complete. On `1 (2 (4, _), 3 (_, 5))` the left and right heights are both 3, so it answers 7 for a tree of 5 nodes.

---

## 5. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Path sum that stops at any node | A non-leaf end is accepted (`1 (2)` with target 1) | Test the sum only at a leaf |
| Pruning when the sum exceeds the target | Wrong with negative values | Never prune |
| Copying the path at every node | `O(n h)` copying | Copy only at a qualifying leaf |
| Forgetting `pop_back` (or the undo `--seen`) | Siblings see each other's nodes | Undo on the way up |
| Map without `{0: 1}` | Paths that start at the root are missed | Seed the empty prefix |
| 32-bit prefix sums or counts | Overflow | `long long` |
| Classic LCA when a node may be absent | Returns the one that exists | Count the nodes found |
| Walking past `p` or `q` to look for the other | Wasted work | Return as soon as one is found |
| LCA of a node with itself | Special-cased wrongly | The recursion handles it |
| Distance K by going down only | Misses nodes through ancestors | Add the parent edge |
| No visited set in the graph search | Goes back to the parent forever | Mark nodes visited |
| `pred` taken as the last node in preorder (flatten) | Extra work or wrong links | Rightmost on the right edge of the left subtree |
| `countComplete` on a non-complete tree | Wrong count | Check the precondition |
| Recursion on a deep tree | Stack overflow | The loops and BFS above |

---

## 6. Questions to answer out loud

1. Why must a root-to-leaf path sum be tested only at a leaf? Why can't you prune on negative values?
2. Why is one shared path vector enough to list all root-to-leaf paths? What does it cost?
3. Explain the prefix-sum count of downward paths. What does the map hold at any moment?
4. What goes wrong without `--seen[prefix]`? Why is `{0: 1}` in the map at the start?
5. Why is the direct count `O(n h)`, and for which tree is it quadratic?
6. State the bottom-up LCA recursion in one sentence. Why does it work when `p` is an ancestor of `q`?
7. What does the classic LCA return if `q` is missing? How does the count version fix that?
8. Explain the two-walker LCA with parent links. How many iterations does it take?
9. How do you get the distance between two nodes from the LCA?
10. How do you turn "nodes at distance K" into a BFS? Why is a visited set needed?
11. In the flatten loop, why is `pred` the rightmost node of the left subtree and not its last node in preorder?
12. Why is counting a complete tree `O(log^2 n)`? What happens if the tree is not complete?

---

## 7. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 7.1 On paper

**Problem A.** On S, which of the sums 5, 18, 22, 26 and 27 have a root-to-leaf path? Which have more than one?

*Where:* paper. *Goal:* five answers. *How:* list the five leaf sums first.

**Problem B.** A left chain has the values 1, -1, 1, -1 from the root down. How many downward paths have sum 0? Write the prefix sums, including the empty one.

*Where:* paper. *Goal:* a number and the list. *How:* equal prefixes give paths.

**Problem C.** In T, find the LCA of (3, 5), (2, 5), (5, 7) and (7, 6), and the distance between 3 and 7.

*Where:* paper. *Goal:* four nodes and a number. *How:* depths first.

**Problem D.** Trace the two-walker method for `p = 3`, `q = 7` in T, and for `p = 5`, `q = 2`. How many iterations does each take?

*Where:* paper. *Goal:* two traces. *How:* `a = parent(a)`, and a null restarts at the other node.

**Problem E.** In T, list the nodes at distance 2 from node 4, and the burn time from node 4 and from node 7.

*Where:* paper. *Goal:* a list and two numbers. *How:* add the parent edge and do a BFS.

**Problem F.** Run the flatten loop on T. For each node that has a left child, write `pred`.

*Where:* paper. *Goal:* three pairs and the final chain. *How:* `pred` follows right pointers only.

**Problem G.** Count the nodes of a complete tree with 6 nodes by the `O(log^2 n)` method. List the nodes at which the function is called, with the two heights of each.

*Where:* paper. *Goal:* the call list and the count. *How:* heap numbering, root 0, children `2i + 1` and `2i + 2`.

**Problem H.** Give a tree for which `countComplete` is wrong because it is not complete, and say what it answers.

*Where:* paper. *Goal:* a tree and a number. *How:* equal outer heights, but a gap inside.

### 7.2 In your editor

For each, write a brute force first, then the version of this tutorial, and compare on all small trees.

1. **Path Sum (112).** Where: LeetCode. Goal: true or false. How: `hasPathSum`; test `1 (2)` and the empty tree.
2. **Binary Tree Paths (257).** Where: LeetCode. Goal: all paths as strings or lists. How: the shared vector.
3. **Path Sum II (113).** Where: LeetCode. Goal: the paths with the sum. How: `pathSumII`.
4. **Path Sum III (437).** Where: LeetCode. Goal: the number of downward paths. How: the direct version, then prefix sums.
5. **Lowest Common Ancestor of a Binary Tree (236).** Where: LeetCode. Goal: the LCA. How: the recursion; then the parent-link form.
6. **All Nodes Distance K in Binary Tree (863).** Where: LeetCode. Goal: the values at distance K. How: parent map and BFS.
7. **Amount of Time for Binary Tree to Be Infected (2385).** Where: LeetCode. Goal: the minutes. How: the BFS rounds.
8. **Flatten Binary Tree to Linked List (114).** Where: LeetCode. Goal: the in-place chain. How: the loop and the recursion.
9. **Count Complete Tree Nodes (222).** Where: LeetCode. Goal: the count in less than `O(n)`. How: compare left and right heights.

**Extra.** Write the LCA for the case where `p` or `q` may be absent (return null), and the distance between two nodes in one bottom-up pass without a parent map.

---

## Answers

**Problem A.** The leaf sums are 27, 22, 26, 22, 18. So 18, 22, 26 and 27 have a path and 5 does not (the root is not a leaf). Only 22 has more than one (5, 4, 11, 2 and 5, 8, 4, 5).

**Problem B.** The prefix sums are 0 (empty), 1, 0, 1, 0. The equal pairs: the three 0 values give 3 pairs, the two 1 values give 1 pair, so 4 paths: (1, -1), (-1, 1), (1, -1) and the whole chain (1, -1, 1, -1).

**Problem C.** LCA(3, 5) = 2, LCA(2, 5) = 2, LCA(5, 7) = 1, LCA(7, 6) = 6. The distance between 3 and 7 is 2 + 2 - 0 = 4.

**Problem D.** For 3 and 7 (equal depths): (2, 6), (1, 1), so 2 iterations, no restarts. For 5 and 2: (4, 1), (2, null), (1, 5), (null, 4), (2, 2): 5 iterations, equal to 3 + 1 - 1 + 2.

**Problem E.** At distance 2 from node 4 are 1 and 3. The burn time from node 4 is 4 (the farthest node is 7); from node 7 it is 5 (the farthest is 5).

**Problem F.** `pred` is 4 for node 1, 3 for node 2 and 5 for node 4. The chain is 1, 2, 3, 4, 5, 6, 7.

**Problem G.** The nodes at which the function is called, with the left and right heights: 0 (3, 2), 1 (2, 2), 2 (2, 1), 5 (1, 1). Node 1 is perfect and counts 3, node 5 counts 1, and node 2 recurses into node 5 and into the missing node 6 (0). The count is 1 + 3 + (1 + 1 + 0) = 6, with 4 calls on non-null nodes.

**Problem H.** `1 (2 (4, _), 3 (_, 5))`: the left height and the right height of the root are both 3, so the function answers 7 for a tree of 5 nodes. The tree is not complete (node 2 has no right child while node 3 has a right child).

**Problems 7.2.** All nine follow the listings. For 437 use `long long`; for 222 check the precondition in the statement.

---

## Cheat sheet

- Root-to-leaf: top-down, subtract the value, test at a leaf only; never prune with negatives. One shared path vector, push and pop, copy at leaves.
- Downward paths with a sum: prefix sums on the current path in a hash map seeded with `{0: 1}`, undo on the way up, 64-bit. `O(n)` instead of `O(n h)`.
- LCA: bottom-up, the first node that gets answers from both sides. The classic version assumes both nodes exist; count them if not.
- LCA with parents: two walkers that restart at the other node meet at the LCA; distance is `depth(p) + depth(q) - 2 depth(lca)`.
- Distance K and burn: add the parent edge, BFS with a visited set; the answer is the frontier after `K` rounds, or the number of rounds.
- Flatten: for each node with a left child, `pred` = rightmost of the left subtree; hang the right subtree there and move the left one to the right.
- Complete tree count: equal leftmost and rightmost heights means perfect (`2^h - 1`), else recurse; `O(log^2 n)`.

---

## Sources

- Titles and numbers of 112, 113, 114, 222, 236, 257, 437 and 863 were checked against [a LeetCode tree problem list](https://howardyangemail.gitbook.io/decode-leetcode/tree) and, for all but 863, a second [list](https://lby-organization.gitbook.io/lby-algorithm-notes/algorithm-topics/tree); the title of 2385 was seen in search results ([write-up](https://dev.to/rohithv07/leetcode-2385-amount-of-time-for-binary-tree-to-be-infected-586i)). Difficulty labels are not used because I could not verify them.
- The prefix-sum idea for subarrays is from the prefix-sums tutorial; the tree version is the same with an undo step.
- All other claims in this tutorial come from the tests and measurements described at the top.
