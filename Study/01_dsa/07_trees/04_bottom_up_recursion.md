# DSA 07.04: Bottom-Up Recursion: Returning Information Up the Tree

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (3 reading, 5 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (the combine-children pattern, height and depth conventions, minimum depth) and [Iterative traversals and Morris traversal](02_iterative_traversals_and_morris.md) (the state-per-frame stack) · Related: [Construction and serialization](03_construction_and_serialization.md) (canonical subtree ids) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- tell whether a tree problem is top-down (information flows down), bottom-up (a summary flows up) or both, and say why;
- write the pair-of-trees recursions: same tree, mirror, symmetric, invert, and the direct subtree test with its cost;
- check balance in `O(n)` instead of `O(n log n)`, and compute the diameter in one pass instead of one pass per node;
- solve maximum path sum by returning one quantity to the parent while recording another on the side;
- run any such bottom-up computation on a tree of a million levels with one non-recursive helper.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 247,715 checks passed under AddressSanitizer with UBSan (GCC 13.3). Each function was compared with a brute force on every tree shape of 0 to 8 nodes (four random labellings each, with negative values and repeats) and on random trees of 20, 60 and 150 nodes: the diameter against a breadth-first search from every node, the maximum path sum against a search of every simple path, balance against the definition, and the pair functions against a mirrored copy. The non-recursive helper ran on chains of one million nodes. The traces come from a separate short Python model. The visit counts are exact. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`. The LeetCode problems are summarised from memory; titles were checked (see Sources) and no difficulty labels are given because none could be verified.

All listings need `<algorithm>`, `<cstdlib>`, `<limits>`, `<utility>` and `<vector>`. The node is the one of tutorial 02:

```cpp
struct Node {
    int val;
    Node* left = nullptr;
    Node* right = nullptr;
};
```

The running example is the tree T of tutorials 02 and 03:

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

## 1. Two directions of recursion

### 1.1 Down or up

A recursive function on a tree sends information in one of two directions.

- **Top-down.** The caller passes something down: the depth so far, a running sum, an allowed range. The function makes its decision with information from the ancestors, and any answer is collected on the way (in a reference parameter). This is the shape of a preorder traversal: the node is dealt with *before* its children.
- **Bottom-up.** Each call *returns* a summary of its subtree, and the parent combines the two summaries with its own value. The node is dealt with *after* its children: postorder. Tutorial 01 section 9 already used this for `height`, `countNodes` and `minDepth`.

The same question, the depth of the tree, in both forms:

```cpp
// top-down: the depth travels down, the answer is collected on the way
inline void depthTopDown(const Node* n, int depth, int& best) {
    if (!n) return;
    best = std::max(best, depth);
    depthTopDown(n->left, depth + 1, best);
    depthTopDown(n->right, depth + 1, best);
}

// bottom-up: each call returns the height of its subtree (in nodes)
inline int depthBottomUp(const Node* n) {
    if (!n) return 0;
    return 1 + std::max(depthBottomUp(n->left), depthBottomUp(n->right));
}
```

Both are `O(n)` time and `O(h)` stack. The first needs a variable outside the recursion to hold the answer; the second needs none, because the answer is the return value. That is the usual advantage of bottom-up: the answer is a function of the children's answers, so it composes.

### 1.2 Choosing

Ask what a node must know to do its job, and where that knowledge lives.

| What the node needs | Direction | Examples |
|---|---|---|
| Something about its ancestors (depth, sum so far, a bound) | Down | Path sum to a target, depth-limited search |
| Something about its subtree (height, size, best value inside) | Up | Balance, diameter, subtree sums |
| Either works | Pick by what else you need | Validating a BST (bounds down, or min and max up, section 1.3) |

### 1.3 A problem that works either way: is it a BST?

A tree is a binary search tree (LeetCode 98) when every value in a node's left subtree is smaller than the node and every value in its right subtree is larger. The common wrong check compares a node only with its two children, which accepts a tree such as `5 (4, 6 (3, 7))`: every parent-child pair is in order, but 3 is in the right subtree of 5.

There are two correct forms, one in each direction:

```cpp
// top-down: the allowed open interval (lo, hi) travels down
inline bool validDown(const Node* n, long long lo, long long hi) {
    if (!n) return true;
    if (n->val <= lo || n->val >= hi) return false;
    return validDown(n->left, lo, n->val) && validDown(n->right, n->val, hi);
}
inline bool isValidBstDown(const Node* root) {                  // LeetCode 98
    return validDown(root, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max());
}

// bottom-up: each call returns whether it is a BST and the smallest and largest value inside
struct BstInfo {
    bool ok;
    long long lo, hi;                                           // empty tree: lo = max, hi = min
};
inline BstInfo bstUp(const Node* n) {
    if (!n) return {true, std::numeric_limits<long long>::max(), std::numeric_limits<long long>::min()};
    BstInfo l = bstUp(n->left);
    BstInfo r = bstUp(n->right);
    bool ok = l.ok && r.ok && l.hi < n->val && n->val < r.lo;
    return {ok, std::min<long long>(l.lo, n->val), std::max<long long>(r.hi, n->val)};
}
inline bool isValidBstUp(const Node* root) { return bstUp(root).ok; }
```

- **Down.** The ancestors decide: the allowed open interval `(lo, hi)` shrinks as you go left (`hi` becomes the node's value) and right (`lo` becomes the node's value). The bounds are `long long` so that a value of `INT_MIN` or `INT_MAX` is never confused with "no bound".
- **Up.** The subtrees report: each call returns whether it is a BST, with its smallest and largest value. The node checks that the left maximum is below it and the right minimum is above it. An empty tree reports `lo` = the largest `long long` and `hi` = the smallest, which makes the comparisons pass without a special case.

Both are `O(n)`. The first carries two numbers down and returns a bool; the second returns three values and carries none. Choosing between them is a matter of taste here, but the first stops at the first violation without examining the rest of the subtree, and the second is the shape you need when you also want the *size of the largest BST inside a tree* (a subtree is a BST and you need to know its size and its extremes).

When the answer for a node is *defined* in terms of the answers for its children, write it bottom-up. When a rule about a node refers to the path from the root, pass that path's summary down. Many hard problems below look top-down at first (they talk about paths from a node), and are bottom-up once you find the right summary.

---

## 2. Two trees at once

Some questions walk two trees in step. The recursion has three cases: both missing (equal), exactly one missing (different), both present (compare the values and recurse on the matching children).

```cpp
inline bool isSame(const Node* a, const Node* b) {              // LeetCode 100
    if (!a || !b) return a == b;                                // both null is equal, one null is not
    return a->val == b->val && isSame(a->left, b->left) && isSame(a->right, b->right);
}

inline bool isMirror(const Node* a, const Node* b) {            // a and b are mirror images
    if (!a || !b) return a == b;
    return a->val == b->val && isMirror(a->left, b->right) && isMirror(a->right, b->left);
}

inline bool isSubtreeNaive(const Node* root, const Node* sub) {   // LeetCode 572, the direct way
    if (!root) return sub == nullptr;
    return isSame(root, sub) || isSubtreeNaive(root->left, sub) || isSubtreeNaive(root->right, sub);
}

inline bool isSymmetric(const Node* root) {                     // LeetCode 101
    return !root || isMirror(root->left, root->right);
}

inline Node* invert(Node* n) {                                  // LeetCode 226
    if (!n) return nullptr;
    Node* l = invert(n->left);
    Node* r = invert(n->right);
    n->left = r;
    n->right = l;
    return n;
}
```

- **`isSame`** (LeetCode 100) pairs left with left and right with right.
- **`isMirror`** pairs left with right and right with left. A tree is **symmetric** (LeetCode 101) when its two subtrees are mirror images.
- **`invert`** (LeetCode 226) swaps the children after inverting them. It could swap first and invert after; any order works because each swap is local. For a deep tree, the stack version below needs no recursion, since the order of the swaps does not matter either:

```cpp
inline void invertIter(Node* root) {                            // no recursion: each swap is local
    std::vector<Node*> st;
    if (root) st.push_back(root);
    while (!st.empty()) {
        Node* n = st.back();
        st.pop_back();
        std::swap(n->left, n->right);
        if (n->left) st.push_back(n->left);
        if (n->right) st.push_back(n->right);
    }
}
```

- **`isSubtreeNaive`** (LeetCode 572) asks `isSame` at every node of the big tree. The cost is the number of node pairs compared. With a root that is a chain of equal values and a `sub` that is a shorter chain of equal values with one different node at the end, no node matches and the work is large. Counted comparisons: 95,050 for a root of 1000 and a `sub` of 100 nodes (plus the odd one), 375,250 for 1000 and 500, and 1,500,500 for 2000 and 1000. Doubling both sizes multiplied the count by 4: it grows like `n * m`. The ids of tutorial 03 section 6.2 make the same question `O(n log n)`.

---

## 3. Returning a summary

### 3.1 Balanced tree

A tree is **balanced** (LeetCode 110) when at every node the heights of the two subtrees differ by at most 1. The direct definition gives the top-down code: at each node, compute both heights, compare, and recurse. The bottom-up code asks each call for the height, with `-1` meaning "somewhere below, a node was unbalanced":

```cpp
inline int height(const Node* n) { return n ? 1 + std::max(height(n->left), height(n->right)) : 0; }

// top-down: recomputes the height of every subtree below every node
inline bool balancedTopDown(const Node* n) {
    if (!n) return true;
    return std::abs(height(n->left) - height(n->right)) <= 1 && balancedTopDown(n->left) &&
           balancedTopDown(n->right);
}

// bottom-up: the height, or -1 as soon as any subtree is unbalanced
inline int heightIfBalanced(const Node* n) {
    if (!n) return 0;
    int l = heightIfBalanced(n->left);
    if (l < 0) return -1;
    int r = heightIfBalanced(n->right);
    if (r < 0) return -1;
    if (std::abs(l - r) > 1) return -1;
    return 1 + std::max(l, r);
}

inline bool isBalanced(const Node* n) { return heightIfBalanced(n) >= 0; }     // LeetCode 110
```

The top-down version computes a height at every node, and each height call walks the whole subtree, so the nodes near the bottom are walked once for every ancestor above them. I counted node visits (calls on a non-empty node, in the instrumented copies):

| Tree | `n` | Height | Top-down visits | Bottom-up visits |
|---|---|---|---|---|
| Perfect | 1023 | 10 | 9217 | 1023 |
| Perfect | 65,535 | 16 | 983,041 | 65,535 |
| Perfect | 1,048,575 | 20 | 19,922,945 | 1,048,575 |
| Minimal AVL shape, height 15 | 1596 | 15 | 16,104 | 1596 |
| Left chain | 1000 | 1000 | 1000 | 1000 |
| Random shape (unbalanced) | 1000 | 22 | 1000 | 19 |

For a perfect tree with `k` levels, `n = 2^k - 1` and the top-down visits are `(k - 1)(n + 1) + 1`: all three perfect rows fit (`9 * 1024 + 1`, `15 * 65,536 + 1`, `19 * 1,048,576 + 1`). That is `n log n`, while the bottom-up code visits each node once. The last two rows show the early exits: both versions stop as soon as they find an imbalance, and which one finds it sooner depends on where it is. The bottom-up code on the random shape found it after 19 visits because the imbalance was on the first path it followed, and the top-down code on the left chain found it at the root after walking the chain once. Time on the perfect tree of 1,048,575 nodes (best of 5): 23.9 ms top-down, 1.4 ms bottom-up.

A top-down check that finds a node unbalanced stops at once, so its worst case is the balanced tree: if every node passes, the tree's height is `O(log n)` and each node is walked by at most `h` ancestors, giving `O(n log n)`. (I argued this bound; the table shows it matching on the shapes above.)

Trace of `heightIfBalanced` on T, in the order the calls finish (postorder). The result of a node is its height in nodes:

| Node | Left height | Right height | Result |
|---|---|---|---|
| 3 | 0 | 0 | 1 |
| 5 | 0 | 0 | 1 |
| 4 | 1 | 0 | 2 |
| 2 | 1 | 2 | 3 |
| 7 | 0 | 0 | 1 |
| 6 | 0 | 1 | 2 |
| 1 | 3 | 2 | 4 |

### 3.2 Diameter

The **diameter** (LeetCode 543) is the number of edges on the longest path between any two nodes. That path need not pass through the root. Look at the highest node of the longest path: it is the node where the path "bends", and the path consists of a downward path into its left subtree and one into its right subtree. The longest such pair has `l.height` edges on the left (the edge to the left child plus the edges below it, which is the left height counted in nodes) and `r.height` edges on the right. So a path that bends at a node has `l.height + r.height` edges, and the answer is the maximum over all nodes. Each call therefore returns two things: the **height** (which the parent needs) and the **best** found so far (which only the root's caller reads).

```cpp
struct DiaInfo {
    int height;                                                 // in nodes
    int best;                                                   // longest path inside, in edges
};

inline DiaInfo diameterInfo(const Node* n) {
    if (!n) return {0, 0};
    DiaInfo l = diameterInfo(n->left);
    DiaInfo r = diameterInfo(n->right);
    return {1 + std::max(l.height, r.height),
            std::max({l.best, r.best, l.height + r.height})};   // a path through n has l + r edges
}

inline int diameter(const Node* root) { return diameterInfo(root).best; }      // LeetCode 543
```

Trace on T:

| Node | Left height | Right height | Edges through it | Best in subtree | Height returned |
|---|---|---|---|---|---|
| 3 | 0 | 0 | 0 | 0 | 1 |
| 5 | 0 | 0 | 0 | 0 | 1 |
| 4 | 1 | 0 | 1 | 1 | 2 |
| 2 | 1 | 2 | 3 | 3 | 3 |
| 7 | 0 | 0 | 0 | 0 | 1 |
| 6 | 0 | 1 | 1 | 1 | 2 |
| 1 | 3 | 2 | 5 | 5 | 4 |

The diameter of T is 5 edges, the path 5, 4, 2, 1, 6, 7. The tempting wrong answer, `height(left) + height(right)` at the root only, is right here and wrong whenever the longest path avoids the root.

The obvious alternative calls `height` at every node, which is the same mistake as in the balance check. Node visits, naive against one pass: 9217 against 1023 on the perfect tree of 1023 nodes (the same counts as the balance check), 1,048,575 nodes: 19,922,945 against 1,048,575, and on a left chain of 1000 nodes 500,500 against 1000. Times: 22.9 ms against 1.9 ms on the perfect tree of 1,048,575 nodes, and 516.1 ms against 0.07 ms on a left chain of 20,000 nodes (best of 5 and best of 3).

### 3.3 Maximum path sum

Now the values can be negative and a path is any sequence of connected nodes, going up and down, with at least one node (LeetCode 124). The idea of the diameter carries over, with two changes: the quantity is a sum, not a length, and a branch with a negative total is better left out.

Each call returns what the parent can *extend*: the best sum of a path that **starts at this node and goes down** (`down`, at least the node's own value). It records on the side the best path that **bends** at this node, using both branches. The two are different, and only `down` can be returned, because a path that already bends at a node cannot be continued upward through it: the parent would reach the node and then use both branches again.

```cpp
struct PathInfo {
    long long down;         // best sum of a path that starts at this node and goes down (>= val)
    long long best;         // best sum of any path inside this subtree
};

inline PathInfo pathInfo(const Node* n) {
    if (!n) return {0, std::numeric_limits<long long>::min()};
    PathInfo l = pathInfo(n->left);
    PathInfo r = pathInfo(n->right);
    long long gl = std::max(0LL, l.down);                       // a negative branch is better left out
    long long gr = std::max(0LL, r.down);
    return {n->val + std::max(gl, gr),
            std::max({l.best, r.best, n->val + gl + gr})};      // the best path that bends at n
}

inline long long maxPathSum(const Node* root) { return pathInfo(root).best; }  // LeetCode 124
```

Trace on the tree `-10` with children `9` and `20`, where `20` has children `15` and `7` (the first example of that problem). The columns `gl` and `gr` are the branch gains after the clipping at 0:

| Node | `gl` | `gr` | Path bending here | `down` returned | Best in subtree |
|---|---|---|---|---|---|
| 9 | 0 | 0 | 9 | 9 | 9 |
| 15 | 0 | 0 | 15 | 15 | 15 |
| 7 | 0 | 0 | 7 | 7 | 7 |
| 20 | 15 | 7 | 42 | 35 | 42 |
| -10 | 9 | 35 | 34 | 25 | 42 |

The answer is 42, the path 15, 20, 7. Notes:

- **All values negative.** The clipping at 0 applies only to the *branches*. The node's own value is always taken, so the answer for `-3` with children `-2` and `-1` is `-1`, the largest single value. The base case for the best is the smallest `long long`, so an empty subtree never wins. Setting it to 0 would return 0 for an all-negative tree (this is one of the broken versions the tests catch).
- **Overflow.** Sums use `long long`. The test with three nodes of 2,000,000,000 has the answer 6,000,000,000, which does not fit in an `int`.

### 3.4 The recipe

All three problems have the same shape. Write it down before the code:

1. **What does the parent need from this subtree?** That is the return value (height, `down`).
2. **What is the answer, and where is it formed?** At each node, from the two returned values and the node's own (the path that bends here). Record it on the side, or return it in the same struct.
3. **Base case.** What is returned for a missing child, and does it make the combine step correct for leaves and for one-child nodes? (Compare the minimum-depth trap of tutorial 01.)
4. **Check the awkward inputs.** The empty tree, a single node, all values negative, a chain.

---

## 4. When the tree is deep

All of the recursions above use stack space `O(h)`. The state-per-frame loop of tutorial 02 section 4 turns any of them into a loop: keep the finished results on a second stack, push the result of a missing child as `empty`, and when a node has seen both children, pop the two results and push `combine(node, left, right)`.

```cpp
// Bottom-up without recursion: combine(node, leftResult, rightResult), empty for a missing child.
template <class T, class F>
T foldPost(const Node* root, const T& empty, F combine) {
    struct Frame { const Node* n; int state; };
    std::vector<Frame> st;
    std::vector<T> vals;                                        // finished results; at most h + 1
    if (root) st.push_back({root, 0});
    while (!st.empty()) {
        const Node* n = st.back().n;
        switch (st.back().state++) {
            case 0:
                if (n->left) st.push_back({n->left, 0});
                else vals.push_back(empty);
                break;
            case 1:
                if (n->right) st.push_back({n->right, 0});
                else vals.push_back(empty);
                break;
            default: {
                T r = std::move(vals.back());
                vals.pop_back();
                T l = std::move(vals.back());
                vals.pop_back();
                vals.push_back(combine(*n, l, r));
                st.pop_back();
            }
        }
    }
    return vals.empty() ? empty : vals.back();
}
```

At most `h + 1` results are on the value stack at one time. With it, the diameter and the maximum path sum become a lambda each:

```cpp
inline int diameterIter(const Node* root) {
    DiaInfo r = foldPost(root, DiaInfo{0, 0}, [](const Node&, const DiaInfo& l, const DiaInfo& r) {
        return DiaInfo{1 + std::max(l.height, r.height), std::max({l.best, r.best, l.height + r.height})};
    });
    return r.best;
}

inline long long maxPathSumIter(const Node* root) {
    PathInfo r = foldPost(root, PathInfo{0, std::numeric_limits<long long>::min()},
                          [](const Node& n, const PathInfo& l, const PathInfo& r) {
        long long gl = std::max(0LL, l.down), gr = std::max(0LL, r.down);
        return PathInfo{n.val + std::max(gl, gr), std::max({l.best, r.best, n.val + gl + gr})};
    });
    return r.best;
}
```

All results of the recursive and the fold versions were equal on every tested tree, and the fold versions ran on chains of one million nodes (left, right and zigzag, node values 1 to 1,000,000) where recursion has no chance: the height was 1,000,000, the diameter 999,999 and the maximum path sum 500,000,500,000, which is `n(n + 1)/2`. The price is speed: on the perfect tree of 1,048,575 nodes the fold diameter took 24.2 ms against 1.9 ms for the recursive one (best of 5, 12 times slower), and on the left chain of 20,000 nodes 0.36 ms against 0.07 ms. I did not profile why. Use the recursive form when the depth is known to be small, and the fold when it is not.

---

## 5. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Calling `height` at every node (balance, diameter) | `O(n log n)` or `O(n^2)` instead of `O(n)` | Return the height and the answer together |
| Diameter taken at the root only | Wrong when the longest path avoids the root | Take the maximum over all nodes |
| Counting nodes where edges are asked (or the other way round) | Off by one | Decide the unit first; here heights are in nodes and the diameter in edges |
| Returning the bent path to the parent | The parent uses a node's two branches twice | Return only the best downward path |
| Not clipping a negative branch at 0 | A bad subtree pulls the sum down | `max(0, branch)` |
| Clipping the node's own value | The answer for an all-negative tree is wrong | Clip branches only |
| Initial best of 0 in max path sum | All-negative trees return 0 | Start from the smallest value |
| `int` sums | Overflow near the limits | `long long` |
| Letting `-1` look like a height in the balance check | A real height and "unbalanced" are mixed up | Return early on `< 0` before combining |
| Checking a BST against parent and children only | Accepts `5 (4, 6 (3, 7))` | Bounds from all ancestors, or subtree min and max |
| Mirror pairing left with left | Symmetric trees are called different | Pair left with right |
| Recursing on a deep tree | Stack overflow | The fold of section 4 |

---

## 6. Questions to answer out loud

1. What is the difference between top-down and bottom-up recursion? Give one problem of each kind and one that works either way.
2. Write the depth of a tree in both forms. Which needs a variable outside the function?
3. Why is the direct balance check `O(n log n)`? Why is it still correct, and what does the bottom-up version change?
4. What does `-1` mean in `heightIfBalanced`, and why must the code test for it before combining?
5. Why must the diameter be a maximum over all nodes? Give a tree whose longest path avoids the root.
6. In maximum path sum, why can the function return only the downward path?
7. Why is a negative branch clipped to 0 but the node's own value is not?
8. What is the answer for a tree whose values are all negative, and where does the code get it from?
9. How do `isSame` and `isMirror` differ?
10. Why is the direct subtree test `O(n * m)` and what replaces it?
11. How would you run the diameter on a chain of a million nodes?

---

## 7. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 7.1 On paper

**Problem A.** For each task, say top-down, bottom-up or either: (1) the number of nodes at depth `d`; (2) is the tree balanced; (3) is there a root-to-leaf path with a given sum; (4) is the tree a valid BST; (5) the diameter; (6) the size of every subtree.

*Where:* paper. *Goal:* six answers with a reason each. *How:* ask what the node needs, and where it lives.

**Problem B.** Add a node 8 as the left child of 7, and a node 9 as the left child of 8, in T. Run `heightIfBalanced`. Which node returns `-1` first?

*Where:* paper. *Goal:* the first unbalanced node and the final result. *How:* the calls finish in postorder.

**Problem C.** Find the diameter of the tree with root 1, left child 2, and no right child, where 2 has the children 3 and 4, 3 has the left child 5 and 4 has the right child 6.

*Where:* paper. *Goal:* a number and the node where the path bends. *How:* try every node; the root is not the best one.

**Problem D.** Find the maximum path sum of the tree with root 5, left child 4 (with child 11, which has the children 7 and 2), and right child 8 (with children 13 and 4, where that 4 has the right child 1).

*Where:* paper. *Goal:* the sum and the path. *How:* at each node add the two non-negative branch gains.

**Problem E.** Is the tree `1 (2 (3, 4), 2 (4, 3))` symmetric? Is `1 (2 (_, 3), 2 (_, 3))`, where `_` is a missing child?

*Where:* paper. *Goal:* two answers with the first failing pair if any. *How:* `isMirror` pairs left with right.

**Problem F.** How many node visits does the top-down balance check make on a perfect tree of 7 nodes and of 15 nodes? And the bottom-up check?

*Where:* paper. *Goal:* four numbers. *How:* use `(k - 1)(n + 1) + 1`, then confirm by counting.

### 7.2 In your editor

For each, write a brute force first, then the bottom-up version, and compare on all small trees.

1. **Maximum Depth of Binary Tree (104).** Where: LeetCode. Goal: the depth. How: both forms of section 1.
2. **Validate Binary Search Tree (98).** Where: LeetCode. Goal: true or false. How: both forms of section 1.3; test with the values `INT_MAX` and `INT_MIN`.
3. **Same Tree (100).** Where: LeetCode. Goal: true or false. How: `isSame`.
4. **Symmetric Tree (101).** Where: LeetCode. Goal: true or false. How: `isMirror`; then an iterative version with a queue of pairs.
5. **Invert Binary Tree (226).** Where: LeetCode. Goal: the mirrored tree. How: the recursive form, then the stack form.
6. **Balanced Binary Tree (110).** Where: LeetCode. Goal: true or false. How: the top-down code, count its visits, then the `-1` code.
7. **Diameter of Binary Tree (543).** Where: LeetCode. Goal: the number of edges. How: `DiaInfo`; check with a tree whose longest path avoids the root.
8. **Binary Tree Maximum Path Sum (124).** Where: LeetCode. Goal: the largest path sum. How: `PathInfo`; test the all-negative tree and `long long`.
9. **Subtree of Another Tree (572).** Where: LeetCode. Goal: true or false. How: `isSubtreeNaive`, then the ids of tutorial 03.

**Extra.** Write the same diameter for an N-ary tree (tutorial 01 section 11): each node returns its height, and the best path through it uses its two tallest children.

---

## Answers

**Problem A.** (1) Down: the depth is passed to each child. (2) Up: balance depends on the heights below. (3) Down: the running sum is passed along the path. (4) Either: the bounds come down from the ancestors, or the minimum and maximum of each subtree go up (section 1.3). (5) Up: height and best path. (6) Up: each size is the sum of the children's sizes plus 1.

**Problem B.** The calls finish in the order 3, 5, 4, 2, 9, 8, 7. Node 9 returns 1; node 8 has the heights 1 and 0, so it returns 2; node 7 has the heights 2 (left, through 8) and 0, which differ by 2, so **node 7 returns -1 first**. Node 6 sees the -1 and returns it at once, and so does node 1: the tree is not balanced.

**Problem C.** The diameter is 4 edges, the path 5, 3, 2, 4, 6, which bends at node 2: the heights of its subtrees are 2 and 2. Through the root, the path has only 3 edges (the height of the left subtree is 3 and the right is empty).

**Problem D.** The maximum path sum is 48, the path 7, 11, 4, 5, 8, 13. At node 11 the branches give 7 + 11 + 2 = 20, returning 18; node 4 returns 22; at 13 and at the right 4 (branch 1) the values are 13 and 5; node 8 returns 21, and at the root 5 the path is 22 + 5 + 21 = 48.

**Problem E.** The first tree is symmetric: the pairs (2, 2), (3, 3) and (4, 4) match after pairing left with right. The second is not: the root's children are two nodes 2 whose left children are missing and whose right children are 3; mirroring pairs the left child of one with the right child of the other, which is missing against 3, so the first failing pair is (missing, 3).

**Problem F.** Top-down: 17 for 7 nodes (`2 * 8 + 1`) and 49 for 15 nodes (`3 * 16 + 1`); the model and the C++ counts agree. Bottom-up: 7 and 15.

**Problems 7.2.** All nine follow the listings. For 543 the unit is edges, so a single node has diameter 0. For 124 a single negative node returns its own value.

**Extra.** Each node returns `1 + the largest child height`; the best path through the node is the sum of its two largest child heights (or one, if it has one child).

---

## Cheat sheet

- BST check: compare with bounds from all ancestors (down) or with the subtree extremes (up), never only with the parent and children.
- Top-down: information about ancestors goes down as parameters. Bottom-up: a summary of the subtree is returned and the parent combines. Many problems use both.
- Pairs: `isSame` pairs left-left and right-right; `isMirror` pairs left-right and right-left; `invert` swaps children, in any order.
- Balanced: return the height, or `-1` for "unbalanced". One pass, `O(n)`; the direct version is `O(n log n)` (for perfect trees `(k - 1)(n + 1) + 1` visits).
- Diameter (edges): at each node `l.height + r.height` (heights in nodes), take the maximum over all nodes, return the height.
- Max path sum: return `down = val + max(0, left, right)`, record `val + max(0, left) + max(0, right)`; start the best from the smallest value; use `long long`.
- Recipe: what does the parent need; where is the answer formed; base case; awkward inputs.
- Deep trees: `foldPost` runs any such combine without recursion, at the price of speed (12 times slower on a perfect tree here).

---

## Sources

- Titles and numbers of 98, 100, 101, 104, 110, 111, 124 and 226 were checked against the [talentd.in tree problem list](https://www.talentd.in/fleetcode/problems?topic=tree); 543 against [a4lamber's tree problem notes](https://a4lamber.github.io/Leetcode/data_structure/tree/problems). The title of 572 was seen in a search result ([doocs, 572](https://leetcode.doocs.org/en/lc/572/)). Difficulty labels are not used because I could not verify them.
- The bound `O(n log n)` for the direct balance check is my own argument, stated in section 3.1; the counts agree with it on the shapes tested.
- All other claims in this tutorial come from the tests and measurements described at the top.
