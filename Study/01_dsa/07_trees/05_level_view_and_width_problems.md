# DSA 07.05: Level, View and Width Problems: Reading a Tree by Rows and Columns

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (3 reading, 5 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (section 8, level order with the width snapshot) and [Bottom-up recursion](04_bottom_up_recursion.md) (the combine pattern, for the contrast in section 1.2) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- write level order in the form that all the variants below grow from, and say when depth-indexed recursion is the better tool;
- produce zigzag order, right and left side views, and the next-right pointers (in `O(1)` extra space, for any tree);
- number the columns of a tree and produce vertical order, top view and bottom view, with the tie rules stated;
- list the boundary of a tree without the classic duplicates;
- compute the maximum width with position indexing and explain exactly where the overflow trap is, and what is and is not a bug.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 134,365 checks passed under AddressSanitizer with UBSan (GCC 13.3). Each function was compared with a separately written brute force on every tree shape of 0 to 8 nodes (three random labellings each), on random trees of 15, 40 and 120 nodes, and on perfect trees of 1 to 8 levels. The references work differently from the listings: levels are collected by asking for one depth at a time, the views come from preorder triples (column, row, value), the boundary from a flag recursion, and the width from 128-bit positions. The next pointers were checked on every level of every tree. The tests also ran chains and zigzag spines of one million nodes. The traces come from a separate short Python model. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`; they are noisy and are only meant to show orders of magnitude. LeetCode statements are summarised from memory; titles were checked (see Sources), and no difficulty labels are given because none could be verified.

All listings need `<algorithm>`, `<cstdint>`, `<map>`, `<tuple>`, `<utility>` and `<vector>`. The node gains a `next` pointer, used only in section 4:

```cpp
struct Node {
    int val;
    Node* left = nullptr;
    Node* right = nullptr;
    Node* next = nullptr;           // only used by section 3
};
```

The running example is the tree T of the earlier tutorials:

```
          1
        /   \
       2     6
      / \     \
     3   4     7
        /
       5
```

---

## 1. Rows

### 1.1 Level order as a loop over levels

Tutorial 01 section 8 wrote level order with a `std::queue` and a width snapshot. Here is the same idea with the snapshot built into the data structure: two vectors, `cur` holds exactly one level and `nxt` collects the next one. There is no counter to get wrong, and the vector is contiguous.

```cpp
// Level order with the width snapshot of tutorial 01, over a plain vector used as a queue.
inline std::vector<std::vector<int>> levelOrder(const Node* root) {
    std::vector<std::vector<int>> levels;
    std::vector<const Node*> cur, nxt;
    if (root) cur.push_back(root);
    while (!cur.empty()) {                                  // cur is exactly one level
        std::vector<int> vals;
        vals.reserve(cur.size());
        nxt.clear();
        for (const Node* n : cur) {
            vals.push_back(n->val);
            if (n->left) nxt.push_back(n->left);
            if (n->right) nxt.push_back(n->right);
        }
        levels.push_back(std::move(vals));
        std::swap(cur, nxt);
    }
    return levels;
}

// Depth-first version: the depth is the index of the level the node belongs to.
inline void levelsDfs(const Node* n, size_t depth, std::vector<std::vector<int>>& levels) {
    if (!n) return;
    if (depth == levels.size()) levels.emplace_back();      // first visit of this depth
    levels[depth].push_back(n->val);
    levelsDfs(n->left, depth + 1, levels);                  // left first keeps left-to-right order
    levelsDfs(n->right, depth + 1, levels);
}
```

Trace on T (each row is one pass of the outer loop):

| Pass | `cur` | Level produced | `nxt` at the end |
|---|---|---|---|
| 1 | 1 | [1] | 2, 6 |
| 2 | 2, 6 | [2, 6] | 3, 4, 7 |
| 3 | 3, 4, 7 | [3, 4, 7] | 5 |
| 4 | 5 | [5] | (empty) |

Every other function in sections 2 to 7 is this loop with a different body: something is recorded per level, or per node together with a number that travels with it (a column, a position).

### 1.2 The same thing by depth

The second function, `levelsDfs`, gets the same result without a queue. The depth of a node is the index of its level, so the recursion carries the depth down (top-down in the sense of tutorial 04), and the first time it sees a new depth it opens a new level. Visiting left before right keeps each level in left-to-right order.

Which to use? The loop needs memory for one level at a time (`O(w)`, `w` the largest level), the recursion needs stack for one root-to-leaf path (`O(h)`). The peak level sizes I measured: 524,288 for a perfect tree of 1,048,575 nodes (height 20), 76,296 for a random tree of 1,000,000 nodes (height 50), and 1 for a chain of 100,000 nodes. The recursion would use 20, 50 and 100,000 frames. So the loop is the safe one for deep, thin trees and the recursion for wide, shallow ones, and a tree you do not know is more often deep than you like. Time, best of 5 (ms):

| Tree | `levelOrder` | `levelsDfs` |
|---|---|---|
| Perfect, 1,048,575 nodes | 13.0 | 6.1 |
| Random, 1,000,000 nodes | 24.4 | 53.3 |
| Chain, 100,000 nodes | 4.2 | 3.4 |

Neither wins everywhere. On the perfect tree, whose nodes my code allocates in preorder, the recursion is about twice as fast; on the random tree, whose nodes are scattered, it is about twice as slow. I did not profile why (a cache effect is my guess, not a measurement).

### 1.3 Small variations

These are one-line edits of the loop; the list is a reminder, not new code.

- **Levels bottom-up** (LeetCode 107): build the levels as above and reverse the outer vector at the end.
- **Average, sum, maximum or count per level** (LeetCode 637 for the average): keep a running value over `cur` instead of a vector. Use a 64-bit sum for the average.
- **First or last node per level**: `cur.front()` and `cur.back()` (section 3).
- **The depth of the tree**: the number of passes (tutorial 04 had a recursive form).

---

## 2. Zigzag

Zigzag level order (LeetCode 103) lists level 1 left to right, level 2 right to left, and so on. The tempting fix is to reverse the queue or to push the children in the other order on alternate levels. Both are wrong: the next level has to be found in its true left-to-right order, because the order of `nxt` is what decides which node comes first on the level after it. The right fix leaves the traversal alone and changes only where each value is written:

```cpp
inline std::vector<std::vector<int>> zigzag(const Node* root) {          // LeetCode 103
    std::vector<std::vector<int>> levels;
    std::vector<const Node*> cur, nxt;
    if (root) cur.push_back(root);
    bool leftToRight = true;
    while (!cur.empty()) {
        std::vector<int> vals(cur.size());
        nxt.clear();
        for (size_t i = 0; i < cur.size(); ++i) {
            const Node* n = cur[i];
            vals[leftToRight ? i : cur.size() - 1 - i] = n->val;      // fill from the right on odd levels
            if (n->left) nxt.push_back(n->left);                     // the queue itself is never reversed
            if (n->right) nxt.push_back(n->right);
        }
        levels.push_back(std::move(vals));
        std::swap(cur, nxt);
        leftToRight = !leftToRight;
    }
    return levels;
}
```

Trace on T: level 1 is written forwards [1]; level 2 backwards, `cur` is (2, 6), so the value of 2 goes to the last slot and the result is [6, 2]; level 3 forwards [3, 4, 7]; level 4 backwards [5]. The answer is [[1], [6, 2], [3, 4, 7], [5]]. The cost is the cost of `levelOrder` (16.4 ms against 13.0 ms on the perfect tree), and no element is moved twice.

---

## 3. Side views

The **right side view** (LeetCode 199) is what you see standing to the right of the tree: for each level, the rightmost node. In the loop it is `cur.back()`; the left side view is `cur.front()`. On T the right view is 1, 6, 7, 5 and the left view is 1, 2, 3, 5.

```cpp
inline std::vector<int> rightSide(const Node* root) {                   // LeetCode 199
    std::vector<int> out;
    std::vector<const Node*> cur, nxt;
    if (root) cur.push_back(root);
    while (!cur.empty()) {
        out.push_back(cur.back()->val);                                  // the last node of the level
        nxt.clear();
        for (const Node* n : cur) {
            if (n->left) nxt.push_back(n->left);
            if (n->right) nxt.push_back(n->right);
        }
        std::swap(cur, nxt);
    }
    return out;
}

// Depth-first: visit the right child first; the first node seen at a new depth is the rightmost.
inline void rightSideDfs(const Node* n, size_t depth, std::vector<int>& out) {
    if (!n) return;
    if (depth == out.size()) out.push_back(n->val);
    rightSideDfs(n->right, depth + 1, out);
    rightSideDfs(n->left, depth + 1, out);
}
```

The second function is the depth-first form: visit the right child first, and the first node reached at a new depth is the rightmost one of its level. It is neat and needs only the result vector, but it recurses to full depth.

A common wrong idea is "follow right children as far as possible". On T that gives 1, 6, 7 and misses 5, which is visible from the right only because the right side of the tree ends early. The same happens with `1 (2 (4), 3)`: the view is 1, 3, 4, and the node 4 is in the left subtree.

---

## 4. Next-right pointers

The task (LeetCode 116 for a perfect tree, 117 for any tree) is to set `next` of every node to the node on its right on the same level, and to null for the last one. With a queue this is `levelOrder` plus a loop. The interesting version uses `O(1)` extra space, by a simple observation: once level `d` is linked, its `next` pointers are the queue of level `d + 1` waiting to be built. You walk level `d` like a linked list and link the children underneath.

```cpp
// Perfect tree (LeetCode 116): every node has two children or none, all leaves on one level.
inline void connectPerfect(Node* root) {
    for (Node* start = root; start && start->left; start = start->left)   // start = leftmost node of a level
        for (Node* n = start; n; n = n->next) {                            // walk that level through its next links
            n->left->next = n->right;                                      // siblings
            if (n->next) n->right->next = n->next->left;                   // cousins across a parent boundary
        }
}

// Any tree (LeetCode 117): O(1) extra space. Walk level d by its next links and build level d + 1.
inline void connect(Node* root) {
    Node* levelStart = root;
    while (levelStart) {
        Node dummy{0};                                                     // anchor for the next level's chain
        Node* tail = &dummy;
        for (Node* n = levelStart; n; n = n->next) {
            if (n->left)  { tail->next = n->left;  tail = tail->next; }
            if (n->right) { tail->next = n->right; tail = tail->next; }
        }
        levelStart = dummy.next;                                           // first node of the level below, or null
    }
}
```

For the perfect tree the two assignments cover everything: two children of one parent are siblings, and the right child of a node is followed by the left child of the node's right neighbour (this is the "cousin" step across a parent boundary). The general version cannot assume that children exist, so it appends whatever children it finds to a chain whose head is a dummy node on the stack, and the next level starts at `dummy.next`.

Result on T: `next` of 2 is 6, of 3 is 4, of 4 is 7, and every other node has a null `next`. The link 4 to 7 is the one that crosses a parent boundary (4 belongs to 2, and 7 to 6). Both functions took 13.3 ms and 19.7 ms on the perfect tree of 1,048,575 nodes, with only a few pointers of extra memory. One more use: after `connect`, the levels can be traversed with no queue at all, by following `next` along a level and `left` or `right` to the start of the next one.

---

## 5. Columns: vertical order, top view, bottom view

Give the root column 0. A left child is one column to the left (`col - 1`) and a right child one column to the right (`col + 1`); the row (depth) grows by one in both directions. On T the columns are -2: [3], -1: [2, 5], 0: [1, 4], 1: [6] and 2: [7]. Two nodes can share both a column and a row (the cousins that meet under a grandparent), and then the output depends on a tie rule. That is where the three problems differ.

```cpp
// One entry per node: its column, its row (depth) and its value.
inline std::vector<std::vector<int>> verticalOrder(const Node* root) {  // LeetCode 987
    struct Item { int col, row, val; };
    std::vector<Item> items;
    struct Q { const Node* n; int col, row; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0, 0});
    while (!cur.empty()) {
        nxt.clear();
        for (const Q& q : cur) {
            items.push_back({q.col, q.row, q.n->val});
            if (q.n->left) nxt.push_back({q.n->left, q.col - 1, q.row + 1});
            if (q.n->right) nxt.push_back({q.n->right, q.col + 1, q.row + 1});
        }
        std::swap(cur, nxt);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return std::tie(a.col, a.row, a.val) < std::tie(b.col, b.row, b.val);   // column, then row, then value
    });
    std::vector<std::vector<int>> out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i == 0 || items[i].col != items[i - 1].col) out.emplace_back();
        out.back().push_back(items[i].val);
    }
    return out;
}
```

**Vertical order** (LeetCode 987) lists the columns from left to right and, inside a column, sorts by row and then by value. The sort key is a tuple, `std::tie(col, row, val)`, so a single `std::sort` does everything and the grouping afterwards is a scan. For T: [[3], [2, 5], [1, 4], [6], [7]]. For the tree `1 (2 (4, 6), 3 (5, 7))` column 0 holds 1 (row 0), 6 and 5 (both row 2), and the answer is [[4], [2], [1, 5, 6], [3], [7]]: the tie between 5 and 6 is broken by value, not by position.

The top view and the bottom view keep one node per column:

```cpp
// Top and bottom view: for each column, the first (top) or the last (bottom) node in level order.
inline std::vector<int> topView(const Node* root) {
    std::map<int, int> firstInColumn;                                      // column -> value
    struct Q { const Node* n; int col; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0});
    while (!cur.empty()) {
        nxt.clear();
        for (const Q& q : cur) {
            firstInColumn.emplace(q.col, q.n->val);                        // emplace keeps the first one
            if (q.n->left) nxt.push_back({q.n->left, q.col - 1});
            if (q.n->right) nxt.push_back({q.n->right, q.col + 1});
        }
        std::swap(cur, nxt);
    }
    std::vector<int> out;
    for (auto& kv : firstInColumn) out.push_back(kv.second);
    return out;
}

inline std::vector<int> bottomView(const Node* root) {
    std::map<int, int> lastInColumn;
    struct Q { const Node* n; int col; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0});
    while (!cur.empty()) {
        nxt.clear();
        for (const Q& q : cur) {
            lastInColumn[q.col] = q.n->val;                                // overwrite: the last one wins
            if (q.n->left) nxt.push_back({q.n->left, q.col - 1});
            if (q.n->right) nxt.push_back({q.n->right, q.col + 1});
        }
        std::swap(cur, nxt);
    }
    std::vector<int> out;
    for (auto& kv : lastInColumn) out.push_back(kv.second);
    return out;
}
```

- **Top view**: the first node of the column in level order (the one closest to the root; among nodes at the same row, the leftmost). `emplace` on an existing key does nothing, which is exactly "keep the first".
- **Bottom view**: the last node of the column in level order, so ties go to the rightmost. `operator[]` overwrites.

For `1 (2 (4, 6), 3 (5, 7))`, the top view is 4, 2, 1, 3, 7 and the bottom view is 4, 2, 5, 3, 7: in column 0, vertical order lists 5 before 6 (by value), while the bottom view picks 5 because it is the later one in level order. If the statement you are given says something different about ties, follow the statement; a different rule is a different comparison and nothing else.

Why level order and not a depth-first walk? A depth-first walk would need the row to decide which node is higher, and then still has to decide ties. Level order delivers rows in increasing order, so "first" and "last" mean "highest" and "lowest" for free. Cost: the sort is `O(n log n)` (133.7 ms for the perfect tree of 1,048,575 nodes, against 13.0 ms for plain level order), the map is `O(n log w)` with `w` columns (21.7 ms for the top view). If the number of columns is bounded you can use an array indexed by `col - minCol` instead; I did not measure that.

---

## 6. Boundary traversal

The boundary is what you meet walking around the outside of the tree anticlockwise from the root: the root, the **left boundary** (down the left edge, leaves excluded), the **leaves** from left to right, and the **right boundary** (leaves excluded) in bottom-up order. Definitions of this problem vary a little between sources (for example in how a root with no left child is treated), so state the definition you use. This one is the common one:

```cpp
inline bool isLeaf(const Node* n) { return !n->left && !n->right; }

// Root, left boundary (no leaves), leaves left to right, right boundary (no leaves) bottom-up.
inline std::vector<int> boundary(const Node* root) {
    std::vector<int> out;
    if (!root) return out;
    out.push_back(root->val);
    if (isLeaf(root)) return out;                                          // a single node is its own boundary
    for (const Node* n = root->left; n; n = n->left ? n->left : n->right)  // go left, else right
        if (!isLeaf(n)) out.push_back(n->val);
    std::vector<const Node*> st{root};                                     // leaves, by an iterative preorder
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        if (isLeaf(n)) { out.push_back(n->val); continue; }
        if (n->right) st.push_back(n->right);
        if (n->left) st.push_back(n->left);
    }
    std::vector<int> rightEdge;
    for (const Node* n = root->right; n; n = n->right ? n->right : n->left)
        if (!isLeaf(n)) rightEdge.push_back(n->val);
    out.insert(out.end(), rightEdge.rbegin(), rightEdge.rend());           // reversed: bottom-up
    return out;
}
```

Rules that the code encodes:

- **The left edge goes left if it can, else right.** A node whose left child is missing still has a left boundary below it, through its right child. The right edge is the mirror image: right if possible, else left.
- **Leaves are never taken from the edges**, so a leaf on the boundary is listed once, in the leaf pass. Without this, the lowest node of each edge appears twice.
- **The root is listed once, and never joins an edge**, so a tree with a single node is just [root].
- **The leaves pass visits every leaf, including the leftmost and rightmost.** It uses an explicit stack, so a deep tree is fine.

Trace on T: root 1; left edge 2 (3 is a leaf, so it stops); leaves 3, 5, 7; right edge 6 (7 is a leaf); reversed. The boundary is [1, 2, 3, 5, 7, 6]. For `1 (_, 2 (3, 4))` it is [1, 3, 4, 2]. The time was 4.5 ms for the perfect tree of 1,048,575 nodes, 51.0 ms for the random tree (cache misses on the leaf walk), and 0.4 ms for the chain.

---

## 7. Maximum width

The **width** of a level (LeetCode 662) is the distance between its leftmost and rightmost nodes *as if the tree were complete*: the gaps count. Number the nodes as in a heap, root at position 0, the left child of `p` at `2p` and the right child at `2p + 1`. Then the width of a level is `last - first + 1`. For T the positions, level by level, are: node 1 at 0; nodes 2 and 6 at 0 and 1; nodes 3, 4 and 7 at 0, 1 and 3 (the missing left child of 6 leaves position 2 empty); node 5 at 2. The widths are therefore 1, 2, 4 and 1, and the answer is 4, although the widest level has only three nodes. For `1 (3 (5, 3), 2 (_, 9))` the third level has positions 0, 1 and 3, and the answer is again 4.

### 7.1 The overflow trap

Positions double at each level, so a node at depth `d` can have a position as large as `2^d - 1`. A right-leaning chain of 64 levels needs a number of 64 bits, and a tree of a few hundred levels (easily built by a test) needs hundreds of bits. A first attempt with signed 64-bit positions:

```cpp
// The version that looks fine and is not: signed positions. Overflow is undefined behaviour.
inline long long maxWidthSigned(const Node* root) {
    struct Q { const Node* n; long long pos; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0});
    long long best = 0;
    while (!cur.empty()) {
        best = std::max(best, cur.back().pos - cur.front().pos + 1);
        nxt.clear();
        for (const Q& q : cur) {
            if (q.n->left) nxt.push_back({q.n->left, 2 * q.pos});
            if (q.n->right) nxt.push_back({q.n->right, 2 * q.pos + 1});
        }
        std::swap(cur, nxt);
    }
    return best;
}
```

Measured on a right-leaning chain with two leaves below its last node, with GCC 13.3 `-O2` and `-fsanitize=signed-integer-overflow`: chains of 60 and 62 nodes passed, and a chain of 64 nodes stopped with `signed integer overflow: 9223372036854775807 * 2 cannot be represented in type 'long long int'`. The same program compiled without the sanitizer (with and without `-fwrapv`) printed the right widths. So the answer was correct only by accident of how the hardware wraps; the program has undefined behaviour, and a compiler is allowed to do anything.

There are two clean fixes, and the question of which is needed is interesting.

```cpp
// Heap numbering: the child of position p is 2p (left) or 2p + 1 (right). Width of a level =
// last position - first position + 1. Unsigned 64-bit arithmetic wraps instead of overflowing.
inline uint64_t maxWidthRaw(const Node* root) {                         // LeetCode 662, no rebasing
    struct Q { const Node* n; uint64_t pos; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0});
    uint64_t best = 0;
    while (!cur.empty()) {
        best = std::max(best, cur.back().pos - cur.front().pos + 1);
        nxt.clear();
        for (const Q& q : cur) {
            if (q.n->left) nxt.push_back({q.n->left, 2 * q.pos});
            if (q.n->right) nxt.push_back({q.n->right, 2 * q.pos + 1});
        }
        std::swap(cur, nxt);
    }
    return best;
}

inline uint64_t maxWidth(const Node* root) {                            // positions rebased per level
    struct Q { const Node* n; uint64_t pos; };
    std::vector<Q> cur, nxt;
    if (root) cur.push_back({root, 0});
    uint64_t best = 0;
    while (!cur.empty()) {
        const uint64_t first = cur.front().pos;                         // the leftmost node becomes position 0
        best = std::max(best, cur.back().pos - first + 1);
        nxt.clear();
        for (const Q& q : cur) {
            const uint64_t p = q.pos - first;
            if (q.n->left) nxt.push_back({q.n->left, 2 * p});
            if (q.n->right) nxt.push_back({q.n->right, 2 * p + 1});
        }
        std::swap(cur, nxt);
    }
    return best;
}
```

- **Unsigned arithmetic (`maxWidthRaw`).** Unsigned overflow is defined: it is arithmetic modulo `2^64`. Differences of positions are then also correct modulo `2^64`, so `last - first + 1` is right whenever the true width is smaller than `2^64`, however far the positions themselves have wrapped. It was equal to the 128-bit reference on every tested tree of depth up to 100, and gave the right answers on zigzag chains of 1,000,000 levels (widths 1 and 2).
- **Rebasing (`maxWidth`).** At every level, subtract the position of the leftmost node from all positions before computing the children. The numbers then stay near the true width of the level and never grow with depth. It gave the same answers as the reference and as the raw version on all tested trees, and it needs one more subtraction per node (18.5 ms for both on the perfect tree: no visible difference).

When is the *answer* too large? When a level really spans more than `2^64` positions, which needs a tree of more than 64 levels with a node far to the left and one far to the right on the same level. Neither version can represent that width; this is a property of the question, not of the code. From memory, the LeetCode statement promises an answer that fits in a 32-bit signed integer, so in that problem the only danger is the intermediate positions, and both fixes are enough.

---

## 8. Putting the measurements together

Best of 5, milliseconds, `-O2`, each on a prebuilt tree. The chain is 100,000 nodes, the other two about one million; the numbers show orders of magnitude, not precise ratios.

| Function | Perfect (1,048,575) | Random (1,000,000) | Chain (100,000) | Extra space |
|---|---|---|---|---|
| `levelOrder` | 13.0 | 24.4 | 4.2 | one level |
| `zigzag` | 16.4 | 25.7 | 3.7 | one level |
| `rightSide` | 10.2 | 21.9 | 0.5 | one level |
| `rightSideDfs` | 11.0 | 46.3 | 0.4 | the stack, `O(h)` |
| `verticalOrder` | 133.7 | 162.0 | 5.7 | all `n` triples |
| `topView` | 21.7 | 52.0 | 17.0 | one level and the map |
| `boundary` | 4.5 | 51.0 | 0.4 | the leaf stack |
| `maxWidth` | 18.5 | 24.2 | 0.4 | one level |
| `connect` | 17.8 | 117.5 | 0.3 | `O(1)` |

Two things stand out. The sort makes `verticalOrder` about ten times the cost of a plain level order. Among the other functions, `connect` is the slowest on the random tree (117.5 ms against 24.4 for `levelOrder`), probably because following `next` pointers jumps around memory; I did not check that.

---

## 9. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Reversing the queue for zigzag | The next level comes out in the wrong order | Change only where values are written |
| "Follow right children" for the right view | Misses nodes visible only from deeper left subtrees | Last node per level |
| Reading the size of the queue inside the inner loop | The level boundary moves as children are pushed | Take the level as a separate vector, or snapshot the size first |
| Sorting a vertical column by position instead of value | Wrong on ties (987 sorts by row, then value) | Sort by `(col, row, val)` |
| Using `emplace` where the last node should win (or the reverse) | Top and bottom views swap their tie rule | `emplace` keeps the first, `operator[]` keeps the last |
| Boundary: leaves in the edges | The lowest edge nodes appear twice | Skip leaves on the edges |
| Boundary: left edge only goes left | Misses a left boundary that continues through a right child | Left if present, else right |
| Boundary: root with one child | Root listed twice, or the missing side handled as an edge | Start edges from the root's children |
| Width counted as the number of nodes | Gaps ignored | `last - first + 1` of positions |
| Signed positions | Undefined behaviour from depth 64 on | Unsigned arithmetic, or rebase each level |
| `int` positions | The same trap at depth 31 | The same fix |
| Relinking `next` with a queue on a huge tree | `O(w)` memory | Use the previous level's `next` links |
| Recursion for views on a deep tree | Stack overflow | The level loop |

---

## 10. Questions to answer out loud

1. Write level order with no counter. Why does the loop with two vectors need no width snapshot?
2. When is the depth-indexed recursion better than the loop, and when is it dangerous?
3. Why is reversing the queue wrong for zigzag? What is the right change?
4. Explain why the right side view is not the right spine.
5. How does the `O(1)`-space next-pointer algorithm work? What is the role of the dummy node?
6. For the perfect tree, what are the two assignments in `connectPerfect`, and why is the second one needed?
7. Define the column of a node. When do two nodes share a column and a row, and how does each of the three problems break the tie?
8. Why does the top view use level order and not a depth-first walk?
9. List the traps of the boundary traversal.
10. Define the width of a level and compute it for a tree you draw. Why do gaps count?
11. Where exactly is the overflow trap in the width, why is signed arithmetic a bug even when it prints the right answer, and why do two different fixes both work?

---

## 11. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 11.1 On paper

**Problem A.** For T, write the level order, the zigzag order, the right and left views, the vertical order, the top and bottom views, the boundary and the width of each level.

*Where:* paper. *Goal:* nine lists. *How:* number the columns and positions first; reuse the traces of this tutorial only to check.

**Problem B.** For the tree `1 (2 (_, 5), 3 (4, _))` (2 is the left child of 1 with the right child 5; 3 is the right child with the left child 4), write the vertical order, the top view and the bottom view. Which node does each of the three choose in column 0?

*Where:* paper. *Goal:* three answers and a one-line reason for column 0. *How:* nodes 5 and 4 share a column and a row.

**Problem C.** For the tree `1 (2 (4 (7)), 3)` (7 is the left child of 4), write the right side view, and say what "follow right children" would give.

*Where:* paper. *Goal:* two lists. *How:* last node per level.

**Problem D.** List the `next` pointer of every node of T after linking. Which links cross a parent boundary?

*Where:* paper. *Goal:* seven pointers and the crossing links. *How:* levels are [1], [2, 6], [3, 4, 7], [5].

**Problem E.** Compute the maximum width of `1 (2 (4, _), 3 (_, 5))`. How many nodes does the widest level have?

*Where:* paper. *Goal:* a number and a count. *How:* positions 0; 0 and 1; 0 and 3.

**Problem F.** Write the boundary of `1 (_, 2 (3, 4))`. Then give a tree for which a boundary that takes only left children on the left edge gives a wrong answer.

*Where:* paper. *Goal:* a list and a tree. *How:* the left edge goes left if it can, else right.

**Problem G.** Positions are signed 64-bit. For a right-leaning chain with two leaves below its last node, what is the greatest number of chain nodes for which `maxWidthSigned` does not overflow?

*Where:* paper (confirm in your editor with `-fsanitize=signed-integer-overflow`). *Goal:* one number. *How:* the node at depth `k` of the chain has position `2^k - 1`.

### 11.2 In your editor

For each, write a brute force first, then the version of this tutorial, and compare on all small trees.

1. **Binary Tree Level Order Traversal (102).** Where: LeetCode. Goal: the levels. How: the loop, then `levelsDfs`.
2. **Binary Tree Level Order Traversal II (107).** Where: LeetCode. Goal: the levels from the bottom. How: reverse at the end.
3. **Average of Levels in Binary Tree (637).** Where: LeetCode. Goal: the average of every level. How: a `long long` sum per level.
4. **Binary Tree Zigzag Level Order Traversal (103).** Where: LeetCode. Goal: the zigzag. How: fill by index.
5. **Binary Tree Right Side View (199).** Where: LeetCode. Goal: the right view. How: both forms; then the left view.
6. **Populating Next Right Pointers in Each Node (116).** Where: LeetCode. Goal: linked levels in `O(1)` space. How: `connectPerfect`.
7. **Populating Next Right Pointers in Each Node II (117).** Where: LeetCode. Goal: the same for any tree. How: `connect` with a dummy.
8. **Vertical Order Traversal of a Binary Tree (987).** Where: LeetCode. Goal: the columns. How: sort by `(col, row, val)`.
9. **Maximum Width of Binary Tree (662).** Where: LeetCode. Goal: the maximum width. How: positions, then rebase; test a deep tree.

**Extra (statements vary, read yours).** Top view, bottom view and boundary traversal are common interview questions that exist in several formulations. Solve them with the definitions of this tutorial, then change the tie rule and see which tests change.

---

## Answers

**Problem A.** Level order [[1], [2, 6], [3, 4, 7], [5]]. Zigzag [[1], [6, 2], [3, 4, 7], [5]]. Right view 1, 6, 7, 5; left view 1, 2, 3, 5. Vertical order [[3], [2, 5], [1, 4], [6], [7]]. Top view 3, 2, 1, 6, 7; bottom view 3, 5, 4, 6, 7. Boundary 1, 2, 3, 5, 7, 6. Widths 1, 2, 4, 1.

**Problem B.** Column 0 holds 1 (row 0) and 5 and 4 (row 2). Vertical order [[2], [1, 4, 5], [3]], top view 2, 1, 3, bottom view 2, 4, 3. Vertical order puts 4 before 5 (by value), the top view takes 1 (the highest), and the bottom view takes 4, because in level order 5 (child of 2) comes before 4 (child of 3), and the last one wins.

**Problem C.** The right view is 1, 3, 4, 7. Following right children gives 1, 3.

**Problem D.** `next`: 1 to null, 2 to 6, 6 to null, 3 to 4, 4 to 7, 7 to null, 5 to null. Only 4 to 7 crosses a parent boundary (4 is a child of 2, and 7 a child of 6).

**Problem E.** The positions are 0 for 1; 0 and 1 for 2 and 3; 0 for 4 and 3 for 5. The level of 4 and 5 has width 3 - 0 + 1 = 4, and it contains only two nodes. The answer is 4.

**Problem F.** The boundary is [1, 3, 4, 2]. A wrong version that takes only left children on the left edge would miss the left boundary of any node with a missing left child: take `1 (2 (_, 3 (4, 5)), 6)`. The left edge goes 2, then 3 (through the right child), and the leaves are 4, 5, 6, so the boundary is [1, 2, 3, 4, 5, 6]; with the wrong rule the edge stops at 2 and 3 is lost.

**Problem G.** The chain nodes have positions 0, 1, 3, 7, ..., so the node at depth `k` has `2^k - 1`. The two leaves below the node at depth `k` have positions up to `2 (2^k - 1) + 1 = 2^(k+1) - 1`, which fits in a signed 64-bit integer only if `k + 1 <= 63`. So the chain can have at most 63 nodes (depths 0 to 62), with the leaves at depth 63. With 64 chain nodes the leaves overflow. This matches the measurement: the run with 64 stopped.

**Problems 11.2.** All nine follow the listings. For 662, build a zigzag chain of a few hundred levels and check that the width is small and the answer unchanged by rebasing.

---

## Cheat sheet

- Level order: two vectors, `cur` and `nxt`; swap per level. Everything else is that loop with a different body.
- Depth-indexed recursion gives the same levels; it uses `O(h)` stack against `O(w)` memory for the loop.
- Zigzag: write each value to `i` or `size - 1 - i`; never reverse the queue.
- Right view: last of each level (left view: first). Depth-first form: right child first, first at a new depth.
- Next pointers in `O(1)` space: walk level `d` by `next`, link level `d + 1` under a dummy.
- Columns: left `-1`, right `+1`. Vertical order sorts by `(col, row, val)`. Top view keeps the first in level order, bottom view the last.
- Boundary: root, left edge (left else right, no leaves), leaves, right edge reversed (no leaves).
- Width: heap positions `2p` and `2p + 1`, width `last - first + 1`; gaps count. Use unsigned arithmetic or rebase each level; signed overflow is undefined.

---

## Sources

- Titles and numbers of 102, 107, 116, 117, 199, 637 and 662 were checked against [a tree problem list](https://notes-algorithm.pages.dev/01-data_structures/200-tree/001-basic-problems.html); the titles of 103 and 987 were seen in search results ([103 notes](https://github.com/sunnala/leetcode/blob/master/problems/103.binary-tree-zigzag-level-order-traversal.md), [987 write-up](https://web.eecs.utk.edu/~jplank/topcoder-writeups/Leetcode/Vertical-Order-Traversal-Of-A-Binary-Tree)). Difficulty labels are not used because I could not verify them.
- The statement that LeetCode 662 promises a 32-bit answer is from memory.
- The top view, bottom view and boundary traversal exist in several formulations; the definitions here are stated in the text and checked against separately written references.
- All other claims in this tutorial come from the tests and measurements described at the top.
