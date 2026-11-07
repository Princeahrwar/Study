# DSA 07.07: BST Core Operations: Search, Insert, Delete and Neighbours

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (3.5 reading, 5.5 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md), [Iterative traversals and Morris traversal](02_iterative_traversals_and_morris.md) (the inorder loop) and [Bottom-up recursion](04_bottom_up_recursion.md) section 1.3 (validating a BST with bounds) · Companion code: [`07_bst_core_operations_demo.cpp`](07_bst_core_operations_demo.cpp) (this is the first search tree of the course, so the structure has its own runnable file; every listing below is also inside it).*

By the end of this tutorial you can:

- state the BST invariant precisely (about whole subtrees, not about parents) and say what policy you chose for duplicate keys;
- write search, insert and delete without recursion and without parent pointers, using the pointer-to-pointer idiom, and handle all three delete cases;
- explain why deleting by relinking nodes differs from copying the successor's key, and what it costs a caller;
- find floor, ceiling, predecessor and successor of a key from the root alone;
- validate a BST correctly, including the `INT_MIN` and equal-key edge cases;
- show with numbers why a BST built from sorted input is a linked list, and what insertion order does to its height.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 2,681,565 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests drove the functions against `std::set<int>` through 60 random sequences of 600 insert, erase and contains operations (small and large key ranges), checking the invariants and every neighbour query at regular points; through **every insertion order of the keys 1 to 7** (5040 trees), deleting each single key by both delete methods and then emptying the tree in a fixed order; through the extreme keys `INT_MIN` and `INT_MAX`; against a second, independently written validator on 4000 random labelled shapes; and on a degenerate tree of one million nodes. The traces and shapes below are the output of the program, copied here. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`. LeetCode statements are summarised from memory; titles were checked (see Sources), and no difficulty labels are given because none could be verified.

The node holds an `int` key (the generalisation to any ordered type is at the end of section 1):

```cpp
struct Node {
    int key;
    Node* left = nullptr;
    Node* right = nullptr;
};
```

The running example B is built by inserting 8, 3, 10, 1, 6, 14, 4, 7, 13 in that order:

```
          8
        /   \
       3     10
      / \      \
     1   6      14
        / \    /
       4   7  13
```

Its inorder walk is 1, 3, 4, 6, 7, 8, 10, 13, 14. In text the same tree is written `8 (3 (1, 6 (4, 7)), 10 (_, 14 (13, _)))`, where `_` is a missing child.

---

## 1. The invariant

A **binary search tree** (BST) is a binary tree in which, for every node `n`, **every key in the left subtree of `n` is smaller than `n.key`, and every key in the right subtree is larger**. The words are *every key in the subtree*. A rule that only compares a node with its two children is weaker and wrong: in `5 (4, 6 (3, 7))` each parent beats its left child and loses to its right child, yet 3 sits in the right subtree of 5. Section 6 turns this into a test.

Three consequences are used constantly:

- **Search is a descent.** At each node one comparison discards one whole subtree.
- **Inorder is sorted.** Left subtree, node, right subtree is increasing order, so an inorder walk of a valid BST lists the keys in ascending order. This gives the cleanest validity test, and the successor and predecessor operations.
- **The cost of every operation is the height `h`**, which is between `log2(n + 1)` and `n`. Section 7 is about which one you get.

**Duplicates** need a policy, stated up front. This tutorial's tree stores each key once: inserting an existing key is refused, like `std::set`. The alternatives are a count in each node, or storing equal keys on one fixed side (as `std::multiset` does, which as I recall keeps equal keys in insertion order). Any statement of a BST problem that allows equal keys must say which side; mixing the two sides breaks search.

Three small helpers are used throughout. `inorder` is the loop of tutorial 02. `height` counts levels with the loop of tutorial 05. `destroy` frees a tree **without recursion**, which matters in section 7: a degenerate tree a million levels deep would overflow the stack of a recursive destructor.

```cpp
inline std::vector<int> inorder(const Node* root) {            // keys in increasing order
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

inline int height(const Node* root) {                          // number of levels, by the level loop of Topic 05
    int h = 0;
    std::vector<const Node*> cur, nxt;
    if (root) cur.push_back(root);
    while (!cur.empty()) {
        ++h;
        nxt.clear();
        for (const Node* n : cur) {
            if (n->left) nxt.push_back(n->left);
            if (n->right) nxt.push_back(n->right);
        }
        std::swap(cur, nxt);
    }
    return h;
}

inline void destroy(Node*& root) {                             // iterative: a degenerate tree must not overflow the stack
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

The functions below work on a plain `Node*` root and keep keys as `int`. To use another key type, make `Node` and the functions templates over `Key` and `Compare` and replace each `<` by the comparator (the equality tests by "neither is less than the other"). That change is mechanical; Topic 09 shows what `std::set` does with it.

---

## 2. Search, minimum and maximum

```cpp
// Search: at each node the key tells you which half cannot contain the answer.
inline const Node* find(const Node* root, int key) {
    const Node* n = root;
    while (n && n->key != key) n = key < n->key ? n->left : n->right;
    return n;                                                  // null if the key is absent
}

inline bool contains(const Node* root, int key) { return find(root, key) != nullptr; }

inline const Node* minNode(const Node* n) {                    // the leftmost node (n must not be null)
    while (n->left) n = n->left;
    return n;
}

inline const Node* maxNode(const Node* n) {                    // the rightmost node (n must not be null)
    while (n->right) n = n->right;
    return n;
}
```

`find` is a loop: if the key is smaller go left, if larger go right, and stop at the key or at null. No recursion is needed because nothing is left to do after the recursive call (it is a tail call). On B, searching for 7 visits 8, 3, 6, 7 and finds it; searching for 5 visits 8, 3, 6, 4 and then falls off the right of 4, so 5 is absent; searching for 13 visits 8, 10, 14, 13. The minimum is the leftmost node and the maximum the rightmost, each a walk of at most `h - 1` steps.

---

## 3. Insert

A new key always becomes a **leaf**: descend as in search, and attach the new node where the descent falls off the tree. The usual way to write this keeps a pointer to the parent and then has to ask "was it the left or the right child?" The pointer-to-pointer idiom removes that question. The variable `link` holds the **address of the pointer that would have to change** (at first the root pointer itself). When the descent reaches a null pointer, `*link = new Node{key}` attaches the node, whichever side it is, and the empty-tree case needs no special code.

```cpp
// Insert without recursion and without a parent pointer: `link` always points at the pointer
// that would have to change, so the new node is attached by one assignment.
inline bool insert(Node*& root, int key) {                     // false if the key is already there
    Node** link = &root;
    while (*link) {
        if (key == (*link)->key) return false;                 // this tree keeps keys unique
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    *link = new Node{key};
    return true;
}
```

On B, `insert(5)` descends 8, 3, 6, 4 and attaches 5 as the right child of 4: `8 (3 (1, 6 (4 (_, 5), 7)), 10 (_, 14 (13, _)))`. The function returns false for a key that is already present. Insert is `O(h)`, and the tree it produces depends on the **order** of the insertions, not only on the set of keys (section 7).

---

## 4. Delete

Deleting is the one operation with cases. Find the node `n` and the pointer `link` that points at it (the same idiom). Then:

1. **No children (a leaf).** Set `*link` to null.
2. **One child.** Set `*link` to that child; the child's subtree takes the place of `n`'s.
3. **Two children.** `n` cannot simply disappear: both subtrees need a parent. Replace it with its **inorder successor** `s`, the leftmost node of the right subtree. `s` is the smallest key larger than `n`, so it is larger than everything on the left and smaller than everything else on the right, and, being leftmost, it has **no left child**, so detaching it is case 1 or 2. (The inorder predecessor, the rightmost node of the left subtree, works symmetrically.)

```cpp
// Erase by relinking: no key is copied, so every other node keeps its address.
inline bool erase(Node*& root, int key) {
    Node** link = &root;                                       // the pointer that points at the node to remove
    while (*link && (*link)->key != key) link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node* n = *link;
    if (!n) return false;
    if (!n->left) {
        *link = n->right;                                      // case 1 and 2: no left child, splice the right one in
    } else if (!n->right) {
        *link = n->left;                                       // case 2: no right child, splice the left one in
    } else {                                                   // case 3: two children
        Node** sl = &n->right;                                 // link to the successor: leftmost of the right subtree
        while ((*sl)->left) sl = &(*sl)->left;
        Node* s = *sl;
        *sl = s->right;                                        // detach it (it has no left child)
        s->left = n->left;                                     // and put it where n was
        s->right = n->right;
        *link = s;
    }
    delete n;
    return true;
}

// The textbook variant: copy the successor's key into n and delete the successor's node instead.
// Shorter, but the key moves between nodes, so a pointer to the successor's node is invalidated.
inline bool eraseCopy(Node*& root, int key) {
    Node** link = &root;
    while (*link && (*link)->key != key) link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node* n = *link;
    if (!n) return false;
    if (n->left && n->right) {
        Node** sl = &n->right;
        while ((*sl)->left) sl = &(*sl)->left;
        Node* s = *sl;
        n->key = s->key;                                       // the copy
        *sl = s->right;
        delete s;
        return true;
    }
    *link = n->left ? n->left : n->right;
    delete n;
    return true;
}
```

The first listing does case 3 by **relinking**: `s` is cut out of the right subtree (`*sl = s->right`), then put where `n` was, taking over `n`'s two children. No key is copied. The second listing, which is what most textbooks show, **copies** `s`'s key into `n` and deletes `s`'s node instead; it is shorter.

Traces on B (the output of the program):

| Delete | Case | Result |
|---|---|---|
| 1 | leaf | `8 (3 (_, 6 (4, 7)), 10 (_, 14 (13, _)))` |
| 10 | one child (14) | `8 (3 (1, 6 (4, 7)), 14 (13, _))` |
| 3 | two children; successor 4 (the leftmost of the subtree rooted at 6) | `8 (4 (1, 6 (_, 7)), 10 (_, 14 (13, _)))` |
| 8 | two children; the root; successor 10 | `10 (3 (1, 6 (4, 7)), 14 (13, _))` |

The two methods give the same tree shape (checked on all 5040 insertion orders of 1 to 7 and every single deletion). They differ in what happens to **other nodes**. With relinking, every surviving node keeps its address: after each of those deletions I compared the address of every remaining key before and after, and none moved. With the copying version, deleting a node with two children moves the successor's key into a different node, so any pointer or reference to the successor's key now points at a deleted node; the test confirmed that the address changed in every two-children case. That matters when other code holds pointers to elements: as far as I know, `std::set` guarantees that erasing one element invalidates only references to that element, which a library must implement by relinking (I did not check the standard's text here; Topic 09 returns to it).

Delete is `O(h)`: one descent to find `n`, and one more to find the successor, both bounded by the height. For the root of B the successor walk is one step long, because 10 has no left child.

---

## 5. Neighbours of a key: floor, ceiling, predecessor and successor

Given a **key** (which may not be in the tree), four questions come up all the time:

| Name | Meaning | `std::set` equivalent |
|---|---|---|
| floor | the greatest key `<=` the given key | `upper_bound(k)`, then one step back |
| ceiling | the smallest key `>=` the given key | `lower_bound(k)` |
| predecessor (lower) | the greatest key `<` the given key | `lower_bound(k)`, then one step back |
| successor (higher) | the smallest key `>` the given key | `upper_bound(k)` |

Tutorial 02 found the successor of a *node* with parent links. From the root, no parent links and no stack are needed: walk down as in search and remember the **last node where the walk turned in the useful direction**. For the ceiling, every node larger than the key is a candidate, and going left looks for a smaller candidate; when the walk ends, the last candidate is the answer.

```cpp
// Neighbours of a KEY, found from the root with no parent pointers and no stack.
// Each walk remembers the last node where it turned the "right" way.
inline std::optional<int> floorKey(const Node* n, int key) {   // greatest key <= key
    std::optional<int> best;
    while (n) {
        if (n->key == key) return key;
        if (n->key < key) { best = n->key; n = n->right; }     // n is a candidate; look for a closer one on the right
        else n = n->left;
    }
    return best;
}

inline std::optional<int> ceilKey(const Node* n, int key) {    // smallest key >= key
    std::optional<int> best;
    while (n) {
        if (n->key == key) return key;
        if (n->key > key) { best = n->key; n = n->left; }
        else n = n->right;
    }
    return best;
}

inline std::optional<int> predecessorKey(const Node* n, int key) {   // greatest key < key
    std::optional<int> best;
    while (n) {
        if (n->key < key) { best = n->key; n = n->right; }
        else n = n->left;                                      // n->key >= key: nothing here or on its right is smaller
    }
    return best;
}

inline std::optional<int> successorKey(const Node* n, int key) {     // smallest key > key
    std::optional<int> best;
    while (n) {
        if (n->key > key) { best = n->key; n = n->left; }
        else n = n->right;
    }
    return best;
}
```

The loops differ only in the comparisons: floor and ceiling stop early on equality, predecessor and successor do not (an equal key is neither smaller nor larger, so the walk continues past it). Results on B (the program's output):

| Key | floor | ceiling | predecessor | successor |
|---|---|---|---|---|
| 5 | 4 | 6 | 4 | 6 |
| 8 | 8 | 8 | 7 | 10 |
| 0 | none | 1 | none | 1 |
| 15 | 14 | none | 14 | none |

Each query is `O(h)` and returns `std::optional<int>`, so "no such key" is not confused with any integer (using `-1` or `INT_MIN` as "none" fails as soon as the tree contains that key; the tests include `INT_MIN` and `INT_MAX`). The results were compared with `std::set` for every probe, including the extremes.

---

## 6. Validating a BST

The invariant is about whole subtrees, so a correct test must see more than a parent and its children. Tutorial 04 section 1.3 gave two recursive forms, bounds going down and minimum and maximum going up. Here is the third, the one that follows from "inorder is strictly increasing": walk the tree inorder with the stack loop and compare each key with the previous one.

```cpp
// Correct: an inorder walk of a BST is strictly increasing, so check that, with no sentinel value.
inline bool isBst(const Node* root) {
    std::vector<const Node*> st;
    const Node* cur = root;
    std::optional<int> prev;                                   // empty until the first key is seen
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        if (prev && cur->key <= *prev) return false;          // <= : equal keys are not allowed here
        prev = cur->key;
        cur = cur->right;
    }
    return true;
}
```

The previous key is an `std::optional<int>` that starts empty. Equal keys are rejected (`<=`), in keeping with this tree's policy; for a tree that allows equal keys on the right, the test would be `<`. Two tempting versions fail, and the test suite runs both on their counterexamples:

```cpp
// Two plausible versions that are wrong. They are tested only to show the counterexamples.
inline bool looksValid(const Node* n) {                        // compares each node with its children only
    if (!n) return true;
    if (n->left && n->left->key >= n->key) return false;
    if (n->right && n->right->key <= n->key) return false;
    return looksValid(n->left) && looksValid(n->right);
}

inline bool isBstSentinel(const Node* root) {                  // inorder walk, but starts from INT_MIN
    std::vector<const Node*> st;
    const Node* cur = root;
    long long prev = INT_MIN;
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        if (cur->key <= prev) return false;                    // rejects a tree whose first key is INT_MIN
        prev = cur->key;
        cur = cur->right;
    }
    return true;
}
```

- `looksValid` compares each node with its children only. It **accepts** `5 (4, 6 (3, 7))`, which `isBst` rejects (3 is in the right subtree of 5).
- `isBstSentinel` starts from `prev = INT_MIN` and rejects `<=`. It **rejects a valid tree** whose smallest key is `INT_MIN`, for instance the single node `INT_MIN`, because nothing is smaller than the sentinel. The same bug appears in bounds-based code that uses `INT_MIN` and `INT_MAX` as "no bound" (that is why tutorial 04 used `long long` bounds).

The inorder test uses `O(h)` extra space, stops at the first violation, and agreed with the bounds version on all 4000 random labelled shapes (keys 0 to 6, so repeats occur) and on the 5040 insertion orders.

---

## 7. Shape: why a BST can be a linked list

Every cost above is `O(h)`. The shape is decided by the order of the insertions: the first key inserted becomes the root, and every later key descends until it falls off the tree. **Sorted input is the worst case**: each new key is larger than everything, so it goes to the right of the last node, and the tree is a right chain. The order 1, n, 2, n - 1, ... (zigzag) is just as bad. Inserting the **median first**, then recursively the medians of each half, produces a perfect tree. Measured for the keys 1 to n (comparisons are the nodes passed while inserting all the keys, and the height is the number of levels at the end):

| `n` | Sorted: comparisons | Sorted: height | Zigzag: comparisons | Zigzag: height | Median first: comparisons | Median first: height | Random order: comparisons | Random order: height |
|---|---|---|---|---|---|---|---|---|
| 15 | 105 | 15 | 105 | 15 | 34 | 4 | 39 | 6 |
| 255 | 32,385 | 255 | 32,385 | 255 | 1538 | 8 | 1889 | 13 |
| 1023 | 522,753 | 1023 | 522,753 | 1023 | 8194 | 10 | 10,851 | 20 |
| 4095 | 8,382,465 | 4095 | 8,382,465 | 4095 | 40,962 | 12 | 53,903 | 25 |

The sorted column is `n(n - 1)/2` (105 for 15, 32,385 for 255). In time, inserting 20,000 sorted keys took 679.0 ms against 1.6 ms for the same keys shuffled, and 50,000 keys took 4748.1 ms against 4.8 ms (best of 3). The random order lies between the extremes. Averaged over 5 random orders, the height was 21.8 for 1000 keys, 40.8 for 100,000 and 52.0 for 1,000,000, against `log2 n` of 10.0, 16.6 and 19.9, and the mean number of nodes passed per insert was 11.04, 20.05 and 25.20, a little below `2 ln n` (13.8, 23.0, 27.6). I only report what was measured; the growth is logarithmic in these runs, with a constant larger than the perfect tree's.

Consequences. If keys arrive sorted or nearly sorted (timestamps, sequence numbers, a file read in order), a plain BST is a linked list with extra pointers. That is the reason for self-balancing trees: Topics 10 to 13 add rotations to keep the height near `log2 n` whatever the order, and Topic 09 shows that `std::set` is one of them. The code in this tutorial never recurses on the tree's depth: `find`, `insert`, `erase`, the neighbour queries, `isBst`, `inorder`, `height` and `destroy` are loops, and the tests run all of them on a one-million-node chain (a recursive destructor or insert would risk the stack, as tutorial 02 measured).

---

## 8. A small owner

Raw `Node*` roots leak and copy badly. This wrapper owns the root, deletes copying, tracks the size, and names the operations after the standard ones (`lower` and `higher` are the strict neighbours, as in Java's `TreeSet`, from memory).

```cpp
// A small owner around the functions above, so a tree cannot leak or be copied by accident.
class Bst {
public:
    Bst() = default;
    Bst(const Bst&) = delete;
    Bst& operator=(const Bst&) = delete;
    ~Bst() { destroy(root_); }

    bool insert(int key) { bool added = ::insert(root_, key); size_ += added; return added; }
    bool erase(int key) { bool gone = ::erase(root_, key); size_ -= gone; return gone; }
    bool contains(int key) const { return ::contains(root_, key); }
    std::optional<int> floor(int key) const { return floorKey(root_, key); }
    std::optional<int> ceil(int key) const { return ceilKey(root_, key); }
    std::optional<int> lower(int key) const { return predecessorKey(root_, key); }
    std::optional<int> higher(int key) const { return successorKey(root_, key); }
    std::optional<int> min() const { return root_ ? std::optional<int>(minNode(root_)->key) : std::nullopt; }
    std::optional<int> max() const { return root_ ? std::optional<int>(maxNode(root_)->key) : std::nullopt; }
    size_t size() const { return size_; }
    const Node* root() const { return root_; }

private:
    Node* root_ = nullptr;
    size_t size_ = 0;
};
```

The inner calls are written `::insert` and `::erase` to reach the free functions rather than the members of the same name. The destructor calls the iterative `destroy`.

---

## 9. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Invariant checked against children only | `5 (4, 6 (3, 7))` is accepted | Compare with the whole subtree: inorder or bounds |
| `INT_MIN` or `INT_MAX` as a sentinel | A valid tree is rejected, or "none" collides with a key | `std::optional`, or wider bounds |
| Insert as recursion returning the subtree, on a huge tree | Stack overflow on a degenerate tree | The pointer-to-pointer loop |
| Forgetting to attach under `link` | The new node is lost | `*link = new Node{key}` |
| Delete with two children: taking the successor from the left subtree | Order broken | Leftmost of the right (or rightmost of the left) |
| Delete: not detaching the successor from its old place | Two parents for one node | `*sl = s->right` before reuse |
| Copying the successor's key while the caller holds pointers | Dangling or wrong pointers | Relink instead |
| Mixed duplicate policy | Search misses equal keys | One rule: reject, count, or one fixed side |
| Floor and ceiling swapped | Off by one in range queries | Floor goes down (`<=`), ceiling up (`>=`) |
| Predecessor stopping on equality | Returns the key itself | Strict comparison, keep walking |
| Using `-1` as "not found" | Collides with a stored key | `std::optional` |
| Recursive destructor | Stack overflow on a chain | Iterative `destroy` |
| Expecting `O(log n)` from a plain BST | Quadratic on sorted input | Balanced trees (Topics 10 to 13) |

---

## 10. Questions to answer out loud

1. State the BST invariant. Why is "each node is between its children" not enough? Give a counterexample.
2. Why is inorder sorted? Use it to give a validity test.
3. How do you handle duplicate keys? What changes in search and insert for each policy?
4. Explain the pointer-to-pointer idiom. What does `link` hold, and why does it remove the empty-tree special case?
5. Walk through the three delete cases. Why does the inorder successor of a node with two children have no left child?
6. What is the difference between deleting by copying the successor's key and by relinking? When does it matter?
7. How do you find the floor of a key with no parent pointers? What is the invariant of the loop?
8. How do predecessor and successor of a key differ from floor and ceiling?
9. Name two bugs of a BST validator involving `INT_MIN`.
10. Why does a BST built from sorted input degenerate? Name two other insertion orders that do, and one that gives a perfect tree.
11. What is the expected height of a BST built from random insertions, from your measurements? Why is that not a guarantee?

---

## 11. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 11.1 On paper

**Problem A.** Insert 50, 30, 70, 20, 40, 60, 80, 35, 45 into an empty BST. Draw it, give its height and its inorder walk, and list the nodes visited when searching for 45.

*Where:* paper. *Goal:* a tree, a number and a path. *How:* each key goes where the descent falls off.

**Problem B.** From the tree of Problem A, delete 20, then 70, then 30, then 50. Draw the tree after each deletion and name the case.

*Where:* paper. *Goal:* four trees and four case names. *How:* in case 3 use the leftmost node of the right subtree.

**Problem C.** For the tree of Problem A, give floor, ceiling, predecessor and successor of the keys 0, 35, 36, 50 and 100.

*Where:* paper. *Goal:* a table of 20 entries. *How:* keep the last candidate while descending.

**Problem D.** Which of these are valid BSTs under this tutorial's rules? (i) `10 (5 (2, 12), 15)`; (ii) `10 (5, 15 (12, 20))`; (iii) `4 (4, _)`; (iv) a single node `INT_MIN`. For each invalid one, name the first violation of an inorder walk.

*Where:* paper. *Goal:* four verdicts. *How:* write the inorder sequence and look for a non-increase.

**Problem E.** Of the 24 orders in which the keys 1, 2, 3, 4 can be inserted, how many give a chain (height 4), and how many give height 3? Describe the orders that give a chain.

*Where:* paper (confirm with a program). *Goal:* two counts and a rule. *How:* each key after the first must be the smallest or the largest so far.

**Problem F.** Delete 50 from the tree of Problem A by copying the successor's key and by relinking. Which node (by its original key) is physically freed in each case, and which keeps the address of the root?

*Where:* paper. *Goal:* two answers and the resulting tree. *How:* the successor of 50 is 60.

**Problem G.** How many comparisons does inserting the keys 1 to 100 in sorted order make (counting the nodes passed)? And for 1000?

*Where:* paper. *Goal:* two numbers. *How:* the key number `i` passes `i - 1` nodes.

### 11.2 In your editor

For each, write a brute force first (a sorted vector and `std::lower_bound`, or `std::set`), then the version of this tutorial, and compare on random operation sequences.

1. **Search in a Binary Search Tree (700).** Where: LeetCode. Goal: the node or null. How: `find`.
2. **Insert into a Binary Search Tree (701).** Where: LeetCode. Goal: the tree with the key added. How: the pointer-to-pointer loop.
3. **Delete Node in a BST (450).** Where: LeetCode. Goal: the tree without the key. How: the three cases; try both methods.
4. **Validate Binary Search Tree (98).** Where: LeetCode. Goal: true or false. How: the inorder test; test `INT_MIN`, `INT_MAX` and equal keys.
5. **Inorder Successor in BST (285).** Where: LeetCode. Goal: the successor of a node. How: `successorKey` (check the statement for whether nodes or keys are given).
6. **Minimum Absolute Difference in BST (530).** Where: LeetCode. Goal: the smallest difference between two keys. How: inorder, compare neighbours.
7. **Minimum Distance Between BST Nodes (783).** Where: LeetCode. Goal: the same idea on another statement. How: as 530.

**Extra.** Add a `size` field to each node and make `erase` and `insert` maintain it; then implement `rank(key)` (the number of keys smaller than `key`) and `select(i)` (the `i`-th smallest) in `O(h)`. This is the order-statistic tree behind Topic 09's `pb_ds` and Topic 30.

---

## Answers

**Problem A.** The tree is `50 (30 (20, 40 (35, 45)), 70 (60, 80))`, height 4, inorder 20, 30, 35, 40, 45, 50, 60, 70, 80. Searching for 45 visits 50, 30, 40, 45.

**Problem B.** Delete 20 (a leaf): `50 (30 (_, 40 (35, 45)), 70 (60, 80))`. Delete 70 (two children, successor 80): `50 (30 (_, 40 (35, 45)), 80 (60, _))`. Delete 30 (one child, 40): `50 (40 (35, 45), 80 (60, _))`. Delete 50 (two children, successor 60): `60 (40 (35, 45), 80)`.

**Problem C.**

| Key | floor | ceiling | predecessor | successor |
|---|---|---|---|---|
| 0 | none | 20 | none | 20 |
| 35 | 35 | 35 | 30 | 40 |
| 36 | 35 | 40 | 35 | 40 |
| 50 | 50 | 50 | 45 | 60 |
| 100 | 80 | none | 80 | none |

**Problem D.** (i) invalid: the inorder walk is 2, 5, 12, 10, 15 and the first violation is 10 after 12 (12 is in the left subtree of 10), although every parent and child pair is ordered. (ii) valid. (iii) invalid under this tutorial's rule: 4 after 4 is not strictly increasing. (iv) valid; the sentinel version rejects it.

**Problem E.** 8 of the 24 orders give a chain: each of the three keys after the first must be the smallest or the largest of those inserted so far, which is `2 * 2 * 2 = 8` choices. The other 16 orders give height 3 (a program confirms: 16 of height 3 and 8 of height 4; no order gives height 2 because 4 keys do not fit in 3 nodes).

**Problem F.** By copying: the node that held 60 is freed, and the root node keeps its address but now holds 60. By relinking: the root node itself is freed, and the node that held 60 becomes the root. Both give `60 (30 (20, 40 (35, 45)), 70 (_, 80))`.

**Problem G.** The key number `i` passes `i - 1` nodes, so the total is `n(n - 1)/2`: 4950 for 100 and 499,500 for 1000.

**Problems 11.2.** All follow the listings. For 450, handle the empty tree and a missing key (return the tree unchanged); for 98 use the inorder test.

---

## Cheat sheet

- Invariant: every key in the left subtree is smaller and every key in the right subtree larger; check it against whole subtrees. Inorder is strictly increasing.
- `find`: go left if smaller, right if larger; `O(h)`.
- Insert: descend with `Node** link`, attach at the null pointer; new keys are leaves.
- Delete: leaf, or splice in the only child, or replace by the leftmost node of the right subtree (relink to keep other nodes' addresses).
- Floor/ceiling/predecessor/successor of a key: one descent, remember the last candidate; return `std::optional`.
- Validate with a running `prev` in an inorder walk (no sentinel), or with bounds in `long long`.
- Shape depends on insertion order: sorted or zigzag input gives a chain (`n(n - 1)/2` comparisons), median-first a perfect tree, random about `log` with a larger constant (height 52 for a million keys in my runs).
- All loops, no recursion on depth; free iteratively.

---

## Sources

- Titles and numbers of 98, 285, 450, 530, 700, 701 and 783 were checked against [a LeetCode BST problem list](https://walkccc.me/LeetCode/topics/data-structures/tree/binary-search-tree). Difficulty labels are not used because I could not verify them.
- The `std::set` erase-invalidation guarantee is stated from memory and marked as such in section 4.
- All other claims in this tutorial come from the tests and measurements described at the top.
