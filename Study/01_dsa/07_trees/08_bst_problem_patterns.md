# DSA 07.08: BST Problem Patterns: Order, Pruning, Construction and Bottom-Up Facts

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (4 reading, 6 problems) · You need: [BST core operations](07_bst_core_operations.md) (search, insert, the invariant, inorder), [Iterative traversals and Morris traversal](02_iterative_traversals_and_morris.md) (the stack loop and threading), [Construction and serialization](03_construction_and_serialization.md) (why two traversals are needed for a general tree) and [Bottom-up recursion](04_bottom_up_recursion.md) (returning facts from a subtree). There is no companion demo file: nothing here is a new data structure, so every listing is plain code in this file.*

By the end of this tutorial you can:

- pick the right tool for a BST question: prune by comparing, walk the inorder order lazily, build from an order, or return facts bottom-up;
- find the k-th smallest key, and run an inorder cursor that holds only the current path (`O(h)` memory), forward and backward;
- solve two-sum on a BST with two cursors in `O(n)` time and `O(h)` memory, and say when a hash set or a vector is the better answer;
- repair a BST in which two keys were swapped, with a stack and with Morris threading in `O(1)` memory;
- find the largest BST hidden inside a general binary tree in one bottom-up pass;
- build a BST from its preorder in `O(n)`, test whether a sequence is a BST preorder, and build a height-balanced BST from a sorted array or a sorted list;
- answer LCA and range sum by descending only the part of the tree that can matter.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 272,689 checks passed under AddressSanitizer with UBSan (GCC 13.3). Every function was compared with a brute force: random BSTs against a sorted vector for every `k` and every two-sum target; **every pair of swapped keys** in 150 random trees of up to 12 keys for both repair methods; 4000 random labelled binary trees against a check of every subtree for the largest BST; **all 40,320 permutations of 1 to 8** as candidate preorders (exactly 1430 are valid, the Catalan number for 8); the balanced build for every size from 0 to 700; and a chain of one million nodes for the non-recursive builder. Traces and counts below are program output (counts marked as such come from a copy of the function with a counter added). The timings are from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`; read them as shapes, not benchmarks. LeetCode statements are summarised from memory; titles were checked (see Sources), and no difficulty labels are given because none could be verified.

The running example B is the tree of Topic 07, built by inserting 8, 3, 10, 1, 6, 14, 4, 7, 13:

```
          8
        /   \
       3     10
      / \      \
     1   6      14
        / \    /
       4   7  13
```

In text it is `8 (3 (1, 6 (4, 7)), 10 (_, 14 (13, _)))`, where `_` is a missing child. Its inorder walk is 1, 3, 4, 6, 7, 8, 10, 13, 14.

---

## 1. Four tools

Almost every BST problem is one of four moves. Name the move first and the code follows.

| Tool | The idea | Problems in this tutorial |
|---|---|---|
| Prune by comparing | At each node one comparison says which subtree can hold the answer; the other is skipped | LCA of a BST, range sum (section 9) |
| Inorder is sorted | A question about order, rank, neighbours or pairs becomes a question about a sorted sequence, read lazily with a stack | k-th smallest, iterator, two-sum, recover (sections 2 to 5) |
| Build from an order | The order names the root; each subtree is a contiguous piece | preorder to BST, sorted array or list to a balanced BST (sections 7 and 8) |
| Facts bottom-up | Each subtree returns a small summary; the parent combines them | largest BST inside a binary tree (section 6) |

How to choose: a comparison of keys means prune; "k-th", "next", "pair", "swapped" mean walk inorder; a given traversal or sorted list to turn into a tree means build; a *general* binary tree with a BST hidden inside means bottom-up, because the other tools need the invariant.

The pieces of Topic 07 that the listings reuse (`insert`, the inorder walk and an iterative `destroy`) are repeated here so the file compiles on its own:

```cpp
struct Node {
    int key;
    Node* left = nullptr;
    Node* right = nullptr;
};

inline bool insert(Node*& root, int key) {                     // Topic 07: false if the key is already there
    Node** link = &root;
    while (*link) {
        if (key == (*link)->key) return false;
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    *link = new Node{key};
    return true;
}

inline std::vector<int> inorder(const Node* root) {            // Topic 07: keys in increasing order
    std::vector<int> out;
    std::vector<const Node*> st;
    const Node* cur = root;
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        out.push_back(cur->key);
        cur = cur->right;
    }
    return out;
}

inline void destroy(Node*& root) {                             // Topic 07: iterative free of a whole tree
    std::vector<Node*> st;
    if (root) st.push_back(root);
    while (!st.empty()) {
        Node* n = st.back();
        st.pop_back();
        if (n->left) st.push_back(n->left);
        if (n->right) st.push_back(n->right);
        delete n;
    }
    root = nullptr;
}
```

---

## 2. K-th smallest key

**The problem** (LeetCode 230): given a BST and `k`, return the k-th smallest key, counting from 1.

The inorder walk visits keys in increasing order, so the answer is the key visited k-th. Run the stack loop of Topic 07 and stop at the k-th pop instead of walking to the end:

```cpp
// k-th smallest key, k counted from 1. The inorder loop of Topic 07, stopped at the k-th visit.
inline std::optional<int> kthSmallest(const Node* root, int k) {
    if (k < 1) return std::nullopt;
    std::vector<const Node*> st;
    const Node* cur = root;
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        if (--k == 0) return cur->key;
        cur = cur->right;
    }
    return std::nullopt;                                       // the tree has fewer than k keys
}
```

Replaying it on B (program output):

| k | Result | Nodes pushed | Nodes popped |
|---|---|---|---|
| 1 | 1 | 3 | 1 |
| 4 | 6 | 5 | 4 |
| 9 | 14 | 9 | 9 |
| 10 | none | 9 | 9 |

For `k = 1` the loop pushes the left path 8, 3, 1 and pops once. For `k = 10` the tree has only 9 keys, so the loop walks everything and returns "none". The cost is `O(h + k)` time and `O(h)` memory.

**The usual follow-up:** the tree changes often and `kth` is called often. Store in each node the number of keys in its subtree. With `s` keys in the left subtree, the answer is in the left subtree if `k <= s`, is this node if `k == s + 1`, and is the `(k - s - 1)`-th key of the right subtree otherwise: `O(h)` per query, with the sizes updated along the insert and delete path. This is the `select` of the Extra of Topic 07 and the order-statistic tree of Topic 30. For the k-th *largest*, mirror the loop (go right first).

---

## 3. A BST iterator with `O(h)` memory

**The problem** (LeetCode 173): implement `next()` and `hasNext()` over the inorder order of a BST, with `O(h)` memory. A first idea is to run the whole inorder walk into a vector and index into it, which costs `O(n)` memory and a full walk before the first answer.

The stack of section 2 can be paused instead of run to the end. Keep exactly the nodes whose key is **still to come**, nearest on top: the path from the root down to the next key, taking only the nodes where the walk went left. `next()` pops the top, and if that node has a right child, pushes the left path of that child.

```cpp
// An inorder cursor that holds only the path to the next key: O(h) memory, not O(n).
// Forward = true walks the keys upward; Forward = false walks them downward.
template <bool Forward>
class BstCursor {
    std::vector<const Node*> st_;                              // the nodes whose key is still to come, nearest on top
    static const Node* nearSide(const Node* n) { return Forward ? n->left : n->right; }
    static const Node* farSide(const Node* n) { return Forward ? n->right : n->left; }
    void pushNear(const Node* n) {
        for (; n; n = nearSide(n)) st_.push_back(n);
    }

public:
    explicit BstCursor(const Node* root) { pushNear(root); }
    bool hasNext() const { return !st_.empty(); }
    int peek() const { return st_.back()->key; }               // precondition: hasNext()
    int next() {                                               // precondition: hasNext()
        const Node* n = st_.back();
        st_.pop_back();
        pushNear(farSide(n));
        return n->key;
    }
    std::size_t depth() const { return st_.size(); }           // for measuring
};
using BstIterator = BstCursor<true>;
using BstReverseIterator = BstCursor<false>;
```

The class is a template on the direction so that the backward cursor is the same code with left and right exchanged (section 4 needs both). On B, after construction the stack holds 8, 3, 1; the first `next()` returns 1 and leaves 8, 3, and the second returns 3 and pushes 6 and 4 (Problem A asks for the rest).

**Cost.** Every node is pushed once and popped once over the whole walk, so `n` calls cost `O(n)`: **amortised `O(1)`** per call, although a single call can cost `O(h)`. The memory is the stack, at most `h`.

Measured with a random-order BST and perfect and degenerate shapes (peak depth of the stack over the whole walk; the number of `next()` calls equals `n` in every row):

| Tree | Keys | Height | Peak stack |
|---|---|---|---|
| random order | 1000 | 24 | 13 |
| random order | 100,000 | 41 | 26 |
| random order | 1,000,000 | 50 | 32 |
| perfect | 131,071 | 17 | 17 |
| left chain | 20,000 | 20,000 | 20,000 |

The peak never exceeds the height, which is the guarantee; for random shapes it is well below it because only left edges are stacked. The honest caveat is the last row: on a degenerate tree `O(h)` is `O(n)`. The cursor holds raw pointers, so the tree must not change while it is alive.

---

## 4. Two-sum on a BST

**The problem** (LeetCode 653): is there a pair of different nodes whose keys sum to `target`?

Three solutions, and a good answer names all three:

| Method | Time | Memory | Needs the BST property |
|---|---|---|---|
| inorder into a vector, then two pointers | `O(n)` | `O(n)` | yes |
| hash set while traversing | `O(n)` | `O(n)` | no |
| forward and backward cursors | `O(n)` | `O(h)` | yes |

```cpp
inline bool twoSumVector(const Node* root, long long target) {   // O(n) time, O(n) memory
    std::vector<int> a = inorder(root);
    for (std::size_t i = 0, j = a.size(); i + 1 < j;) {
        long long s = (long long)a[i] + a[j - 1];
        if (s == target) return true;
        if (s < target) ++i; else --j;
    }
    return false;
}

inline bool twoSumHash(const Node* root, long long target) {     // O(n) time, O(n) memory, any tree
    std::unordered_set<long long> seen;
    std::vector<const Node*> st;
    if (root) st.push_back(root);
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        if (seen.count(target - n->key)) return true;
        seen.insert(n->key);
        if (n->left) st.push_back(n->left);
        if (n->right) st.push_back(n->right);
    }
    return false;
}

inline bool twoSumCursors(const Node* root, long long target) { // O(n) time, O(h) memory
    BstIterator lo(root);
    BstReverseIterator hi(root);
    if (!lo.hasNext()) return false;
    long long a = lo.next(), b = hi.next();                    // the smallest and the largest key
    while (a < b) {                                            // a < b: two different nodes
        long long s = a + b;
        if (s == target) return true;
        if (s < target) a = lo.next();                         // b is larger than a, so lo has a next key
        else b = hi.next();
    }
    return false;
}
```

The cursor version is the two-pointer scan over the sorted keys with the two "pointers" realised as the two cursors of section 3. Invariant: all keys smaller than `a` and larger than `b` were already ruled out. If `a + b < target`, no key `b' <= b` can pair with `a`, so `a` is dropped; if it is larger, `b` is dropped. The loop stops when the cursors meet, and `a < b` guarantees two different nodes. Sums are computed in `long long`, because two keys near `INT_MAX` overflow an `int` (the tests use `INT_MAX` and `INT_MIN` as keys).

On B (program output): the target 13 is found (6 + 7), 22 is found (8 + 14), 28 is not (14 + 14 would need the same node twice), 100 is not, and all three methods agree. For the target 21 the cursors compute (1,14) = 15, (3,14) = 17, (4,14) = 18, (6,14) = 20 and (7,14) = 21, found.

Measured with 100,000 random-order keys, all even, and an odd target so that no pair matches (the worst case):

| Quantity | Value |
|---|---|
| `next()` calls by the two cursors together | 100,001 (each key is consumed about once) |
| peak combined stack of the two cursors | 41 (the tree height is 44) |
| vector method: elements stored | 100,000 |
| time per call, mean of 20 runs | vector 2.756 ms, hash set 8.452 ms, cursors 2.510 ms |

The cursors are not faster than the vector; they save memory, 41 pointers instead of 100,000 keys. The hash set was slower in this run (a hash and an allocation per key), but it is the only method that works on a general binary tree. Give the trade-off in the interview, not one winner.

---

## 5. Recover a BST in which two keys were swapped

**The problem** (LeetCode 99): exactly two nodes of a BST (distinct keys) had their keys swapped. Restore the BST without changing its shape.

The inorder walk of a valid BST is increasing. Swap two entries of an increasing sequence and look at what happens:

- if the two were **neighbours** in the order, one descent appears: a key followed by a smaller key, and those two are exactly the swapped pair;
- otherwise **two descents** appear. The larger swapped key now stands too early and is followed by something smaller (first descent); the smaller swapped key now stands too late and follows something larger (second descent).

So: the first node is the *left* element of the *first* descent, and the second node is the *right* element of the *last* descent. In the adjacent case there is one descent and the same rule picks both of its elements. Swap their keys.

On B (program output), three swaps:

| Swapped | Inorder after the swap | Descents | Picked (first, second) |
|---|---|---|---|
| 3 and 13 | 1 13 4 6 7 8 10 3 14 | (13,4) and (10,3) | 13 and 3 |
| 6 and 7 | 1 3 4 7 6 8 10 13 14 | (7,6) | 7 and 6 |
| 4 and 14 | 1 3 14 6 7 8 10 13 4 | (14,6) and (13,4) | 14 and 4 |

All of them are restored to `8 (3 (1, 6 (4, 7)), 10 (_, 14 (13, _)))`. The root can be involved too: swapping 8 and 10 gives one descent (10,8) and is repaired the same way.

```cpp
// Two keys of a BST with distinct keys were swapped. In the inorder walk they show up as descents
// (a key followed by a smaller one): one descent if the two were neighbours, two otherwise.
inline bool recoverBst(Node* root) {                           // swaps the two keys back; false if none was out of place
    std::vector<Node*> st;
    Node *cur = root, *prev = nullptr, *first = nullptr, *second = nullptr;
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        if (prev && prev->key > cur->key) {                    // a descent
            if (!first) first = prev;                          // the larger key of the first descent was moved too early
            second = cur;                                      // the smaller key of the last descent was moved too late
        }
        prev = cur;
        cur = cur->right;
    }
    if (first) std::swap(first->key, second->key);
    return first != nullptr;
}

// The same with Morris threading: O(1) extra memory, and the tree is restored before it returns.
inline bool recoverMorris(Node* root) {
    Node *cur = root, *prev = nullptr, *first = nullptr, *second = nullptr;
    auto visit = [&](Node* n) {
        if (prev && prev->key > n->key) {
            if (!first) first = prev;
            second = n;
        }
        prev = n;
    };
    while (cur) {
        if (!cur->left) {
            visit(cur);
            cur = cur->right;
        } else {
            Node* p = cur->left;
            while (p->right && p->right != cur) p = p->right;  // the predecessor of cur
            if (!p->right) { p->right = cur; cur = cur->left; }   // thread it, go left
            else { p->right = nullptr; visit(cur); cur = cur->right; }   // second arrival: unthread, visit
        }
    }
    if (first) std::swap(first->key, second->key);
    return first != nullptr;
}
```

The stack version uses `O(h)` memory. The Morris version uses `O(1)`: it threads each node's inorder predecessor back to the node, visits in inorder, and removes the thread on the second arrival (the technique of Topic 02). Two cautions that cost people points: it **must run to the end** (an early exit would leave a thread in the tree, which is then a cycle), and during the walk the tree is not valid for any other reader, so it is not safe with concurrent readers.

The code swaps *keys*, which is what the usual statement allows; if other structures point at the nodes, the detection is the same but the repair must swap node positions.

In the tests every swap was repaired to the exact original shape by both methods, the number of descents was 1 exactly when the keys were neighbours in the order, and an untouched tree returned "nothing to fix". The rule needs distinct keys (a descent is a strict `>`).

---

## 6. The largest BST inside a binary tree

**The problem** (LeetCode 333): the tree is a general binary tree. A *subtree* is a node with all its descendants. Return the number of nodes of the largest subtree that is a BST (strictly: smaller keys left, larger keys right).

The direct way asks, for each node from the top, "is this whole subtree a BST?" and returns its size at the first yes (a BST contains only smaller BSTs, so the first yes from the top is the largest on that path). Each check costs `O(size)`:

```cpp
// Largest BST inside a binary tree (the keys here are arbitrary, not a BST).
inline bool validBounds(const Node* n, long long lo, long long hi) {
    if (!n) return true;
    if (n->key <= lo || n->key >= hi) return false;
    return validBounds(n->left, lo, n->key) && validBounds(n->right, n->key, hi);
}
inline int countNodes(const Node* n) { return n ? 1 + countNodes(n->left) + countNodes(n->right) : 0; }

// top-down: the first subtree that is a BST is the largest one below it
inline int largestBstNaive(const Node* n) {
    if (!n) return 0;
    if (validBounds(n, LLONG_MIN, LLONG_MAX)) return countNodes(n);
    return std::max(largestBstNaive(n->left), largestBstNaive(n->right));
}

// bottom-up: every node is looked at once
struct BstInfo {
    bool isBst;
    int size;                      // nodes in this subtree
    long long lo, hi;              // smallest and largest key, meaningful only when isBst
    int best;                      // size of the largest BST inside this subtree
};

inline BstInfo largestBstUp(const Node* n) {
    if (!n) return {true, 0, LLONG_MAX, LLONG_MIN, 0};         // an empty tree is a BST of size 0
    BstInfo l = largestBstUp(n->left);
    BstInfo r = largestBstUp(n->right);
    int size = l.size + r.size + 1;
    if (l.isBst && r.isBst && l.hi < n->key && n->key < r.lo)
        return {true, size, std::min<long long>(l.lo, n->key), std::max<long long>(r.hi, n->key), size};
    return {false, size, 0, 0, std::max(l.best, r.best)};
}

inline int largestBstSubtree(const Node* root) { return largestBstUp(root).best; }
```

`largestBstNaive` re-reads the lower nodes once per ancestor that fails, which is quadratic when the failures are late. `largestBstUp` is the bottom-up pattern of Topic 04: each call returns whether its subtree is a BST, its size, its smallest and largest key, and the best size seen inside. A node is the root of a BST exactly when both children are BSTs, `l.hi < key` and `key < r.lo`; the facts of a failed subtree are not used by its parent except through `best`.

The example (program output) is `10 (5 (1, 8), 15 (7, 20 (17, 25)))`. The left subtree `5 (1, 8)` is a BST of size 3. The right subtree `15 (7, 20 (17, 25))` is a BST of size 5 (keys 7 to 25). The whole tree is not a BST, although every parent and child pair is ordered at the root: the key 7 is in the right subtree of 10 and is smaller than 10. The answer is 5.

Worst case for the naive version (counted with a copy of each function that adds one per node touched): a right chain with the keys 1, 2, ..., n - 1 and a final node with key 0. Every suffix looks like a BST until its last node, so each ancestor reads its whole suffix and fails.

| n | Node touches, naive | Node touches, bottom-up | Time, naive | Time, bottom-up |
|---|---|---|---|---|
| 1000 | 500,501 | 1000 | 0.77 ms | 0.004 ms |
| 4000 | 8,002,001 | 4000 | 12.87 ms | 0.014 ms |
| 20,000 | 200,010,001 | 20,000 | 323.23 ms | 0.169 ms |

(Times are the mean of 3 runs.) The naive count grows with `n^2 / 2`, the bottom-up count with `n`. Both recurse to depth `h`; for a degenerate tree of a million nodes use `foldPost` of Topic 04. *Maximum Sum BST in Binary Tree* (1373) is the same function with a `sum` field.

---

## 7. Build a BST from its preorder

**The problem** (LeetCode 1008): given the preorder of a BST with distinct keys, rebuild it. Unlike a general binary tree (Topic 03), one traversal is enough, because the inorder walk is the keys sorted.

The preorder is: the root, then every key smaller than the root (the left subtree, itself in preorder), then every key larger (the right subtree). Inserting the keys one by one with the `insert` of Topic 07 is correct, but each key costs its depth, and splitting the vector at the first larger key copies elements again and again. The linear way passes an **upper bound** down: a call consumes the next key only if it is below the bound, otherwise it returns null and the caller tries that key as a right child.

```cpp
// Preorder of a BST with distinct keys -> the tree. A key is consumed by the first call
// whose upper bound allows it, so every key is looked at once: O(n).
inline Node* buildBounded(const std::vector<int>& pre, std::size_t& i, long long hi) {
    if (i == pre.size() || pre[i] >= hi) return nullptr;
    Node* n = new Node{pre[i++]};
    n->left = buildBounded(pre, i, n->key);                    // smaller keys follow, below this key
    n->right = buildBounded(pre, i, hi);                       // then the larger ones, below the bound inherited from above
    return n;
}
inline Node* fromPreorder(const std::vector<int>& pre) {
    std::size_t i = 0;
    return buildBounded(pre, i, LLONG_MAX);
}

// The same without recursion: the stack holds the nodes that can still get a right child.
inline Node* fromPreorderIter(const std::vector<int>& pre) {
    if (pre.empty()) return nullptr;
    Node* root = new Node{pre[0]};
    std::vector<Node*> st{root};
    for (std::size_t i = 1; i < pre.size(); ++i) {
        Node* n = new Node{pre[i]};
        Node* parent = nullptr;
        while (!st.empty() && st.back()->key < n->key) { parent = st.back(); st.pop_back(); }
        if (parent) parent->right = n;                         // the last node smaller than n gets it as right child
        else st.back()->left = n;                              // otherwise n is the left child of the top
        st.push_back(n);
    }
    return root;
}

// Is this sequence the preorder of some BST (distinct keys)?
inline bool isBstPreorder(const std::vector<int>& pre) {
    std::vector<int> st;
    long long lo = LLONG_MIN;                                  // every later key must exceed the last key we left behind
    for (int x : pre) {
        if (x <= lo) return false;
        while (!st.empty() && st.back() < x) { lo = st.back(); st.pop_back(); }
        if (!st.empty() && st.back() == x) return false;       // a repeated key
        st.push_back(x);
    }
    return true;
}
```

Trace of `buildBounded` on 8 3 1 6 4 7 10 14 13: the first call (bound infinity) takes 8. Its left call (bound 8) takes 3, and 3's left call (bound 3) takes 1. The two calls under 1 meet 6, which is not below their bounds (1 and 3), so both return null. Back at 3, the right call (bound 8) takes 6; under 6 the left call (bound 6) takes 4 and the right call (bound 8) takes 7. Under 7 the next key, 10, is not below 7 or 8, so both calls return null, and the returns climb to 8's right call (bound infinity), which takes 10; 10's left call (bound 10) sees 14 and returns null, its right call takes 14, and 14's left call (bound 14) takes 13. The result is B. The stack version needs no recursion: the stack holds the nodes that can still get a right child, in decreasing order of key from the bottom; a new key pops every node smaller than itself, and the last popped node becomes its parent on the right, or, if nothing was popped, the new key is the left child of the top. Both builders give B for this input.

**Is this sequence a BST preorder at all** (LeetCode 255)? The same stack gives a test: each time a key pops nodes, the largest popped node becomes a lower bound that every later key must exceed, since we have moved into its right subtree.

Program output for `isBstPreorder`: `8 3 1 6 4 7 10 14 13` is valid. `5 3 6 4` is not (4 follows 6, so it should be in the right subtree of 5, but is smaller than 5). `5 2 6 1` is not, `5 5` is not, `3 5 3` is not, `5 3 5` is not.

Measured (the insert column counts nodes passed by repeated `insert`, from a copy with a counter; the builder makes `2n + 1` calls, `n` that take a key and `n + 1` that return null):

| Input | n | Repeated insert passes | Bounded builder calls |
|---|---|---|---|
| preorder of a chain (sorted keys) | 1000 | 499,500 | 2001 |
| the same | 4000 | 7,998,000 | 8001 |
| preorder of a random-order BST | 1000 | 10,770 (10.77 per key) | 2001 |
| the same | 100,000 | 1,968,673 (19.69 per key) | 200,001 |

For random trees insertion is only a constant factor worse; for a chain it is quadratic. The recursive builder has depth `h`, so a sorted preorder of a million keys would overflow the stack, which the stack version handled in the test. The 1430 valid permutations of 1 to 8 round-trip through both builders.

The preorder is also enough to serialise a BST (the idea of LeetCode 449): write the keys in preorder and read them back with the builder; no null markers are needed, unlike the general format of Topic 03.

---

## 8. Sorted array or list to a balanced BST

**The problem** (LeetCode 108 and 109): make a height-balanced BST (the heights of the two subtrees of every node differ by at most 1) from sorted keys. Inserting the keys in order gives a chain (Topic 07), so choose the root deliberately: the middle key, then recurse on the two halves.

```cpp
// Sorted array -> height-balanced BST: the middle key is the root, the halves are the subtrees.
inline Node* sortedToBst(const std::vector<int>& a, int lo, int hi) {   // the half-open range [lo, hi)
    if (lo >= hi) return nullptr;
    int mid = lo + (hi - lo) / 2;
    Node* n = new Node{a[mid]};
    n->left = sortedToBst(a, lo, mid);
    n->right = sortedToBst(a, mid + 1, hi);
    return n;
}

// The same shape from a sequence that can only be read forward (a linked list): build the left
// subtree first, so the next key to read is exactly the root. O(n) time, no middle search.
template <class It>
Node* inorderBuild(It& it, int n) {                            // takes the next n keys from `it`
    if (n == 0) return nullptr;
    Node* left = inorderBuild(it, n / 2);
    Node* root = new Node{*it};
    ++it;
    root->left = left;
    root->right = inorderBuild(it, n - n / 2 - 1);
    return root;
}
```

`mid = lo + (hi - lo) / 2` avoids the overflow of `(lo + hi) / 2`, the same habit as in binary search. For an even number of keys it takes the upper of the two middle keys, so the left half is the larger. Program output for the keys 1 to n:

| n | Tree |
|---|---|
| 1 | `1` |
| 2 | `2 (1, _)` |
| 3 | `2 (1, 3)` |
| 4 | `3 (2 (1, _), 4)` |
| 6 | `4 (2 (1, 3), 6 (5, _))` |
| 7 | `4 (2 (1, 3), 6 (5, 7))` |
| 10 | `6 (3 (2 (1, _), 5 (4, _)), 9 (8 (7, _), 10))` |

**Why the height is `ceil(log2(n + 1))`.** The two halves of `n - 1` keys have `floor(n / 2)` and `n - 1 - floor(n / 2)` keys, so their sizes differ by at most 1. The height obeys `h(n) = 1 + h(floor(n / 2))`, which solves to `floor(log2 n) + 1`, the same number. Since the sizes of the halves differ by at most 1, so do their heights, so every node is balanced. Measured, with the formula checked for every `n` up to 700: n = 1000 gives 10, 1023 gives 10, 1024 gives 11, and 1,000,000 gives 20.

**A list cannot be indexed**, and finding the middle of a list again and again costs `O(n log n)` in total. `inorderBuild` avoids it by building in the order of the final inorder walk: it builds the left subtree of `n / 2` keys first, which consumes exactly the keys that precede the root, so the next key of the sequence *is* the root. Then it builds the right subtree from the rest. It runs in `O(n)` time with `O(log n)` stack, reads the sequence once from the front, and gives the same shape as `sortedToBst` (tested with a `std::forward_list`).

*Balance a Binary Search Tree* (1382) is the composition of what you now have: the inorder walk of the unbalanced tree into a vector, then `sortedToBst`. A tree that must *stay* balanced under inserts needs the rotations of Topics 10 to 13.

---

## 9. Pruning descents: LCA and range sum

When the question compares keys with the node's key, each node discards one side.

```cpp
inline const Node* lcaBst(const Node* root, int p, int q) {    // both keys must be in the tree
    int lo = std::min(p, q), hi = std::max(p, q);
    const Node* n = root;
    while (n) {
        if (hi < n->key) n = n->left;                          // both are on the left
        else if (lo > n->key) n = n->right;                    // both are on the right
        else return n;                                         // lo <= key <= hi: the paths split here
    }
    return nullptr;
}

inline long long rangeSum(const Node* n, int lo, int hi) {     // sum of the keys in [lo, hi]
    if (!n) return 0;
    if (n->key < lo) return rangeSum(n->right, lo, hi);        // this key and its left subtree are too small
    if (n->key > hi) return rangeSum(n->left, lo, hi);         // this key and its right subtree are too large
    return n->key + rangeSum(n->left, lo, hi) + rangeSum(n->right, lo, hi);
}
```

**LCA of a BST** (LeetCode 235). Walk down while both keys are on the same side. The first node whose key lies in `[lo, hi]` is the lowest common ancestor: every node above it had both keys strictly on one side, and at it the paths split (or it is one of the two keys; a node counts as its own descendant). The general LCA of Topic 06 reads all `n` nodes; this loop reads one path. The precondition is that both keys are in the tree; if they might not be, check with two `find` calls first (`O(h)` each), otherwise the loop returns the split node for keys that do not exist.

On B (program output): `lcaBst(4, 7)` is 6, `(1, 7)` is 3, `(13, 14)` is 14, `(7, 13)` is 8 and `(3, 4)` is 3.

Measured on a random-order BST of 1,000,000 keys (height 50): 2000 random pairs visited on average only **2.35** nodes, because two random keys are usually split near the root; 2000 pairs of *adjacent* keys `(k, k + 1)` visited on average **24.32** nodes, because they stay together almost to the bottom. The worst case is the height; a mean quoted without the query distribution means little.

**Range sum** (LeetCode 938). A node smaller than `lo` and its whole left subtree are out of range, so only the right subtree is visited; symmetric for a node larger than `hi`. The cost is `O(h + m)` for `m` keys in the range. Measured on the same tree (counted with a copy that adds one per node): a range holding 100 keys visited 126.2 nodes on average, a range holding 100,000 keys visited 100,026.0. The extra nodes are the two boundary paths; a subtree sum stored in each node would answer in `O(h)`.

On B: `rangeSum(4, 10)` is 35, the sum of 4, 6, 7, 8 and 10.

The same discarding solves *Trim a Binary Search Tree* (669: a node below `lo` is replaced by its trimmed right subtree, a node above `hi` by its trimmed left one, and the discarded nodes must be freed), *Closest Binary Search Tree Value* (270) and *Split BST* (776).

---

## 10. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| k-th: counting from 0, or returning after the loop | Off by one | `--k == 0` at the pop, `k` from 1 |
| Iterator: pushing both children | Wrong order, too much memory | Push only the near spine; the far child when popped |
| Two-sum: `a + b` in `int` | Overflow near `INT_MAX` | `long long` sums |
| Two-sum: allowing `a == b` | A node paired with itself (28 on B) | Loop while `a < b` |
| Recover: taking both nodes of the first descent in the non-adjacent case | Wrong pair swapped | First: left of the first descent; second: right of the last |
| Morris with an early exit | A thread left in the tree | Run to the end |
| Largest BST: checking parent and child only | Accepts `10 (5 (1, 8), 15 (7, ...))` as a BST at the root | Compare with `l.hi` and `r.lo` of the whole subtree |
| Preorder build by splitting a vector | Copies and quadratic time | An upper bound and one index |
| Preorder check ignoring the lower bound | Accepts `5 3 6 4` | Track the largest popped key |
| Balanced build by repeated insert of sorted keys | A chain | Pick the middle |
| LCA: continuing past the split | A wrong, too-high answer | Return at the first node in `[lo, hi]` |
| Trim: unlinking without freeing | Leaks every discarded node | `destroy` the discarded part |

---

## 11. Questions to answer out loud

1. Name the four tools of section 1 and one problem for each. How do you tell from a statement which to use?
2. How do you find the k-th smallest key in `O(h + k)`? What do you change to answer many queries on a changing tree?
3. State the invariant of the stack in the BST iterator. Why is `next()` amortised `O(1)` but not worst-case `O(1)`? When is the memory not `O(log n)`?
4. Describe the three solutions to two-sum on a BST. When does each win?
5. Why does the two-cursor scan never skip a pair?
6. Two keys of a BST were swapped. What does the inorder walk look like in the adjacent and non-adjacent cases, and which nodes do you pick?
7. How does Morris threading give `O(1)` memory, and why must the loop never exit early?
8. Why is the naive "largest BST" quadratic, and which four facts does each call of the bottom-up version return?
9. Why is one traversal enough to rebuild a BST? Explain the upper-bound recursion, and how to test that a sequence is a BST preorder.
10. Why does the middle element give a balanced tree? What is the height as a formula of `n`, and how do you build it from a linked list?
11. Where does the LCA descent stop, and what happens if one key is absent?

---

## 12. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 12.1 On paper

**Problem A.** For tree B, write the stack depth of the forward cursor after construction and after each of the nine `next()` calls.

*Where:* paper. *Goal:* a list of depths. *How:* `next()` pops one node and pushes the left path of its right child.

**Problem B.** In tree B swap (i) the keys 4 and 14, (ii) the keys 10 and 13. For each, give the inorder walk, the descents, the two nodes picked by the algorithm, and say whether the swap was of neighbours in the order.

*Where:* paper. *Goal:* two walks and two pairs. *How:* the first node is the left element of the first descent, the second is the right element of the last.

**Problem C.** Give the size of the largest BST in `7 (4 (2, 5), 9 (6, 12 (10, 14 (_, 11))))` and name the node whose subtree it is. Which node is the first one whose subtree fails, and why?

*Where:* paper. *Goal:* a size and a node. *How:* compute `(isBst, size, lo, hi, best)` bottom-up.

**Problem D.** Which of these are preorders of a BST: (i) 10 5 2 7 15 12 20; (ii) 10 5 2 12 7 15; (iii) 4 2 1 3 6 5 7; (iv) 3 1 4 2? For each valid one draw the tree; for each invalid one name the key that breaks it.

*Where:* paper. *Goal:* four verdicts. *How:* keep the stack and the lower bound as in `isBstPreorder`.

**Problem E.** Build the balanced BST of the keys 1 to 12 with `sortedToBst`. Give the root, the height, and the sizes of the two subtrees of the root. For which `n` up to 16 is the result a perfect tree?

*Where:* paper. *Goal:* a tree and a list. *How:* the root is the key at index `12 / 2`.

**Problem F.** How many nodes does inserting the keys 1, 2, ..., 100 one by one pass, and how many calls does `buildBounded` make for the same preorder? What are the numbers for 1000?

*Where:* paper. *Goal:* four numbers. *How:* the key number `i` passes `i - 1` nodes; the builder makes `2n + 1` calls.

**Problem G.** For tree B and the target 28, list every sum the two-cursor scan computes and the values of `a` and `b` when it stops.

*Where:* paper. *Goal:* a list of sums. *How:* the pair (14, 14) is never tried because the loop needs `a < b`.

### 12.2 In your editor

For each, write the brute force first (a sorted vector, or the definition applied to every node), then the version of this tutorial, and compare on random trees.

1. **Kth Smallest Element in a BST (230).** Where: LeetCode. Goal: the k-th key. How: `kthSmallest`; then subtree sizes for the follow-up.
2. **Binary Search Tree Iterator (173).** Where: LeetCode. Goal: `next` and `hasNext` in `O(h)` memory. How: `BstIterator`.
3. **Two Sum IV - Input is a BST (653).** Where: LeetCode. Goal: true or false. How: all three methods; time them on a big tree.
4. **Recover Binary Search Tree (99).** Where: LeetCode. Goal: the repaired tree. How: the descent rule; stack version, then Morris.
5. **Largest BST Subtree (333)** and **Maximum Sum BST in Binary Tree (1373).** Where: LeetCode. Goal: a size and a sum. How: `largestBstUp` with a sum field.
6. **Construct Binary Search Tree from Preorder Traversal (1008)** and **Verify Preorder Sequence in Binary Search Tree (255).** Where: LeetCode (check whether your account can open 255). Goal: a tree and a verdict. How: the bound recursion, the stack version, `isBstPreorder`.
7. **Convert Sorted Array to Binary Search Tree (108)**, **Convert Sorted List to Binary Search Tree (109)** and **Balance a Binary Search Tree (1382).** Where: LeetCode. Goal: balanced trees. How: `sortedToBst`, `inorderBuild`, and inorder plus `sortedToBst`.
8. **Lowest Common Ancestor of a Binary Search Tree (235)**, **Range Sum of BST (938)** and **Trim a Binary Search Tree (669).** Where: LeetCode. Goal: a node, a number and a tree. How: the pruning rules of section 9.
9. **Convert BST to Greater Tree (538).** Where: LeetCode. Goal: each key replaced by itself plus all larger keys. How: a reverse inorder walk with a running sum.

**Extra.** Implement *Binary Search Tree Iterator II* (1586), which also has `hasPrev` and `prev`. A stack alone cannot go back; decide what extra state you need and what it costs in memory. Then compare it with the two-cursor design of section 4.

---

## Answers

**Problem A.** After construction 3 (stack 8, 3, 1). After the calls returning 1, 3, 4, 6, 7, 8, 10, 13, 14 the depths are 2, 3, 2, 2, 1, 1, 2, 1, 0.

**Problem B.** (i) Swapping 4 and 14 gives `8 (3 (1, 6 (14, 7)), 10 (_, 4 (13, _)))`; the inorder walk is 1 3 14 6 7 8 10 13 4, the descents are (14,6) and (13,4), the picked nodes are 14 and 4, and the keys are not neighbours in the order. (ii) Swapping 10 and 13 gives inorder 1 3 4 6 7 8 13 10 14 with one descent (13,10); the picked nodes are 13 and 10, and they are neighbours (10 and 13 are consecutive in the sorted order). Both are repaired to B.

**Problem C.** The largest BST has 3 nodes: the subtree rooted at 4 (`4 (2, 5)`). The subtree of 14 is the first to fail, since its right child 11 is smaller than 14; so every ancestor of 14 (12, 9 and 7) fails, even though 12's other child is fine. The subtrees of 10, 6 and 11 are single nodes.

**Problem D.** (i) valid: the tree is `10 (5 (2, 7), 15 (12, 20))`. (ii) invalid: after 12 (a right child of 10) the key 7 appears, but every later key must exceed 10. (iii) valid: `4 (2 (1, 3), 6 (5, 7))`. (iv) invalid: after 4 (which pops 1 and 3 and makes the lower bound 3) the key 2 appears, below the bound.

**Problem E.** The root is 7; the tree is `7 (4 (2 (1, 3), 6 (5, _)), 10 (9 (8, _), 12 (11, _)))`, height 4 (`ceil(log2 13)`), with 6 keys on the left and 5 on the right. The result is a perfect tree for n = 1, 3, 7 and 15, the numbers of the form `2^h - 1`.

**Problem F.** Insertion passes `n(n - 1)/2` nodes: 4950 for 100 and 499,500 for 1000. The builder makes 201 calls for 100 and 2001 for 1000.

**Problem G.** The sums are (1,14) = 15, (3,14) = 17, (4,14) = 18, (6,14) = 20, (7,14) = 21, (8,14) = 22, (10,14) = 24 and (13,14) = 27, eight in all. Then `a` becomes 14, equal to `b`, and the loop stops with no match.

**Problems 12.2.** All follow the listings. For 99, test with both adjacent and distant swaps and with the root involved; for 109 test the empty list and a single node; for 538 and 1382 compare with a brute force on random trees.

---

## Cheat sheet

- Four tools: prune by comparing, walk inorder lazily, build from an order, return facts bottom-up.
- k-th smallest: inorder loop, stop at the k-th pop; many queries: store subtree sizes (`O(h)` per query).
- Iterator: stack of the near spine; `next` pops and pushes the far child's near spine; amortised `O(1)`, memory `O(h)`.
- Two-sum: vector (`O(n)` memory), hash set (any tree), or forward and backward cursors (`O(h)` memory); sums in `long long`; `a < b`.
- Recover: first = left of the first descent, second = right of the last; one descent means neighbours; Morris gives `O(1)` memory and must run to the end.
- Largest BST in a binary tree: return `(isBst, size, lo, hi, best)`; the naive version is quadratic.
- Preorder to BST: pass an upper bound, one index, `O(n)`; or a stack of nodes that can still get a right child. Validity: track the largest popped key.
- Sorted array to balanced BST: the middle is the root, height `ceil(log2(n + 1))`; a list: build left, take the next key, build right.
- BST LCA: the first node with a key in `[lo, hi]`; range sum and trim discard whole subtrees; cost `O(h + answer)`.

---

## Sources

- Titles and numbers of 99, 108, 109, 173, 230, 235, 255, 270, 333, 449, 538, 653, 669, 776, 938, 1008, 1373, 1382 and 1586 were checked against [a LeetCode BST problem list](https://walkccc.me/LeetCode/topics/data-structures/tree/binary-search-tree). Difficulty labels are not used because I could not verify them, and I did not check which of the problems need a paid account.
- All other claims in this tutorial come from the tests and measurements described at the top.
