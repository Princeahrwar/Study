# DSA 07.02: Iterative Traversals and Morris Traversal

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (3 reading, 5 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (the three recursive orders, Euler tour, the n + 1 null links) and [Stack and queue fundamentals](../05_stacks_and_queues/01_stack_and_queue_fundamentals.md) · Related: [Recursion fundamentals](../06_recursion_and_backtracking/01_recursion_fundamentals.md) (what a call frame costs) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- say how deep a recursive traversal can go on a given stack, and why a tree from sorted input is the dangerous case;
- write preorder, inorder and postorder with an explicit stack, and explain why postorder is the hard one;
- turn any of the three recursive orders into one loop with a state per stack frame, and write a lazy inorder iterator;
- write Morris traversal in all three orders, explain why it needs no stack, and list what it costs;
- walk a tree in order with parent links only, as the kernel's `rb_next` does, and choose between the five methods.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 258,660 checks passed under AddressSanitizer with UBSan (GCC 13.3). Each traversal was compared with a recursive reference on 300 random shapes for every size from 0 to 40 nodes, larger random trees, perfect trees and short chains, and on one-million-node chains where recursion cannot be used. After every Morris run the test compared every child pointer with a snapshot taken before. The traces in the tables come from a separate short Python model. Stack sizes and step counts are exact. The timings and depth limits were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2` (except where `-O0` is named). The LeetCode problems are summarised from memory; titles were checked (see Sources) and no difficulty labels are given because none could be verified.

All listings need `<utility>` and `<vector>`. The node is the `TreeNode<int>` of tutorial 01 without the template, so the listings stay short:

```cpp
struct Node {
    int val;
    Node* left = nullptr;
    Node* right = nullptr;
};
```

The running example is this tree, called T. Its three orders are inorder 3 2 5 4 1 6 7, preorder 1 2 3 4 5 6 7 and postorder 3 5 4 2 7 6 1.

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

## 1. Why leave recursion

### 1.1 What a frame costs

The recursive inorder of tutorial 01 uses one call frame per level of the tree, so its stack use is proportional to the height `h`, not to `n`. I measured how deep it can go. The function was `void inorderRec(const Node*, long long& sum)`; the tree was a chain of nodes built on the heap, and the stack limit was set with `setrlimit` in a child process. The depth is the largest chain that did not crash (found by binary search).

| Stack limit | `-O0`, either chain | `-O2`, left-leaning chain | `-O2`, right-leaning chain |
|---|---|---|---|
| 8 MiB (the Linux default) | 260,863 | 469,763 | survived 40 million |
| 1 MiB | 31,487 | 56,888 | survived 40 million |
| 64 KiB | 3,839 | 7,118 | survived 40 million |

At `-O0` the frame is 32 bytes (8 MiB / 32 = 262,144), at `-O2` about 18 bytes. The right-leaning chain survives at `-O2` only because GCC turned the last call into a jump; the language does not promise that, and it is gone at `-O0` and in any version where work follows the call. A real traversal with a callback or more local variables has a bigger frame and fewer levels.

### 1.2 Who has deep trees

- **Unbalanced trees built from sorted or hostile input.** A plain BST fed sorted keys is a chain: `h = n`. For comparison, a perfect tree of one million nodes has `h = 20`, and a random shape of 100,000 nodes (each new node attached to a random free slot) had `h = 43` in my run.
- **Small stacks.** At 32 bytes per level, a 2 KiB task stack (a size often seen in real-time systems, from memory) holds at most 64 levels. Kernel code runs on a small fixed stack too: a 2014 post on the kernel mailing list (see Sources) reports kernel stack overflows on x86-64 with an 8 KiB stack and proposes 16 KiB, which arm64 already had. Kernel code therefore keeps its stack use small; section 7 shows how its red-black tree is walked.

### 1.3 Three ways out

| Method | Extra space | Changes the tree | Section |
|---|---|---|---|
| Explicit stack | `O(h)` on the heap | no | 2, 3, 4, 5 |
| Morris threading | `O(1)` | yes, temporarily | 6 |
| Parent links | `O(1)` | no, but each node stores a parent | 7 |

An explicit stack moves the same `O(h)` memory from a fixed-size call stack to a heap that can grow. The two other methods remove the `O(h)`; the recursion they replace is the baseline.

---

## 2. Inorder and preorder with an explicit stack

The recursive inorder does three things: go left, visit, go right. The explicit version makes the "go left" part a loop that pushes every node it passes, because each of those nodes must be visited after its left subtree and is waiting for it.

```cpp
template <class F>
void inorderIter(Node* root, F visit) {
    std::vector<Node*> st;
    Node* cur = root;
    while (cur || !st.empty()) {
        while (cur) {                 // go left as far as possible
            st.push_back(cur);
            cur = cur->left;
        }
        cur = st.back();              // the leftmost unvisited node
        st.pop_back();
        visit(cur->val);              // inorder: visit when popped
        cur = cur->right;             // then do its right subtree
    }
}

template <class F>
void preorderIter(Node* root, F visit) {
    std::vector<Node*> st;
    Node* cur = root;
    while (cur || !st.empty()) {
        while (cur) {
            visit(cur->val);          // preorder: visit when pushed
            st.push_back(cur);
            cur = cur->left;
        }
        cur = st.back();
        st.pop_back();
        cur = cur->right;
    }
}
```

Read `inorderIter` as the recursion with the stack made visible: the inner `while` is the chain of calls going left, `pop_back` is returning from the call on the left child, and `cur = cur->right` is the second call. The stack holds the nodes whose left subtree is being processed. Preorder is the same loop with one line moved: the visit happens when a node is pushed, not when it is popped.

Trace of `inorderIter` on T. The stack is shown bottom to top, after the pop:

| Visit | Stack after the pop |
|---|---|
| 3 | 1 2 |
| 2 | 1 |
| 5 | 1 4 |
| 4 | 1 |
| 1 | empty |
| 6 | empty |
| 7 | empty |

Each node is pushed once and popped once, so the time is `O(n)`, and the stack never holds more than `h` nodes (peaks for several shapes are in section 8).

There is a second popular form of preorder that visits at the pop and pushes the right child before the left one, so the left child comes out first:

```cpp
template <class F>
void preorderPush(Node* root, F visit) {
    std::vector<Node*> st;
    if (root) st.push_back(root);
    while (!st.empty()) {
        Node* n = st.back();
        st.pop_back();
        visit(n->val);
        if (n->right) st.push_back(n->right);   // right first, so left pops first
        if (n->left) st.push_back(n->left);
    }
}
```

It is shorter, and on a left-leaning chain of 1000 nodes its stack peaked at 1 where the first form's peaked at 1000, but it does not extend to inorder: the first form is the one to learn because all three orders can be written in that shape.

---

## 3. Postorder is the hard one

In inorder and preorder a node is dealt with the first or second time you reach it. In postorder a node is visited the third time: after the left subtree, after the right subtree. When you pop a node from the stack you cannot tell whether you have already done its right subtree. There are two standard fixes.

```cpp
template <class F>
void postorderLast(Node* root, F visit) {
    std::vector<Node*> st;
    Node* cur = root;
    Node* last = nullptr;             // the node visited most recently
    while (cur || !st.empty()) {
        if (cur) {
            st.push_back(cur);
            cur = cur->left;
            continue;
        }
        Node* top = st.back();
        if (top->right && top->right != last) {
            cur = top->right;         // right subtree not done yet
        } else {
            visit(top->val);          // both subtrees done
            last = top;
            st.pop_back();
        }
    }
}

template <class F>
void postorderReverse(Node* root, F visit) {
    std::vector<Node*> st;
    std::vector<int> rev;             // root, right, left order
    if (root) st.push_back(root);
    while (!st.empty()) {
        Node* n = st.back();
        st.pop_back();
        rev.push_back(n->val);
        if (n->left) st.push_back(n->left);     // left first, so right pops first
        if (n->right) st.push_back(n->right);
    }
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) visit(*it);
}
```

- **`postorderLast`** remembers the node visited most recently. A node is ready to visit when it has no right child, or when `last` is that right child (we have just come back from it). Its stack holds only the path from the root, so it never exceeds `h`.
- **`postorderReverse`** uses the fact that postorder (left, right, root) is the reverse of "root, right, left". That order is the preorder with the children swapped, so a preorder loop collects it and the result is read backwards. The classic "two stacks" version is the same idea, with the second stack as the output. The price is a buffer of `n` values.

Trace of `postorderLast` on T, with the stack before the pop (bottom to top) and the value of `last` before the visit:

| Visit | Stack before the pop | `last` |
|---|---|---|
| 3 | 1 2 3 | none |
| 5 | 1 2 4 5 | 3 |
| 4 | 1 2 4 | 5 |
| 2 | 1 2 | 4 |
| 7 | 1 6 7 | 2 |
| 6 | 1 6 | 7 |
| 1 | 1 | 6 |

At the visit of 4 there is no right child, so it is ready. At the visit of 2 the right child 4 equals `last`, so it is ready; without the `last` test, node 2 would send the loop into its right child again and again.

---

## 4. One loop for all three orders

The recursion has three places where work can happen: before the left call, between the calls and after the right call. Give each stack frame a state that says which of the three it is in, and one function covers every order:

```cpp
enum class Order { Pre, In, Post };

template <class F>
void walk(Node* root, Order order, F visit) {
    struct Frame { Node* n; int state; };
    std::vector<Frame> st;
    if (root) st.push_back({root, 0});
    while (!st.empty()) {
        Node* n = st.back().n;
        switch (st.back().state++) {
            case 0:                                   // about to go left
                if (order == Order::Pre) visit(n->val);
                if (n->left) st.push_back({n->left, 0});
                break;
            case 1:                                   // back from the left
                if (order == Order::In) visit(n->val);
                if (n->right) st.push_back({n->right, 0});
                break;
            default:                                  // back from the right
                if (order == Order::Post) visit(n->val);
                st.pop_back();
        }
    }
}
```

The state is the "program counter" of the recursive call: 0 means about to go left, 1 means back from the left, 2 means back from the right. `visit` appears in exactly the place it would have in the recursive code, so the three orders differ in one test each. Note that the code copies `n` before the `switch`, because `push_back` can reallocate the vector and invalidate any reference to `st.back()`.

Many interview solutions use a flag instead of a state: when a node is popped unexpanded, push its parts in the reverse of the wanted order, with the node itself marked `expanded`; when popped expanded, visit it.

```cpp
template <class F>
void walkFlag(Node* root, Order order, F visit) {
    std::vector<std::pair<Node*, bool>> st;          // (node, expanded?)
    st.push_back({root, false});
    while (!st.empty()) {
        auto [n, expanded] = st.back();
        st.pop_back();
        if (!n) continue;
        if (expanded) {
            visit(n->val);
            continue;
        }
        // push in the reverse of the wanted order
        switch (order) {
            case Order::Pre:
                st.push_back({n->right, false});
                st.push_back({n->left, false});
                st.push_back({n, true});
                break;
            case Order::In:
                st.push_back({n->right, false});
                st.push_back({n, true});
                st.push_back({n->left, false});
                break;
            case Order::Post:
                st.push_back({n, true});
                st.push_back({n->right, false});
                st.push_back({n->left, false});
                break;
        }
    }
}
```

Changing the order means changing the order of three `push_back` calls, which is easy to remember under pressure. Its stack is about twice as large (section 8).

**Where this pays off.** Any recursion that does work after its calls, such as the height or the diameter (topic 04), becomes a loop with this device: the state says which child you are back from, and the frame holds the partial results.

---

## 5. A lazy iterator

Sometimes you want one element at a time, with the traversal paused in between: the next value of a BST, a merge of two trees, the kth smallest. Keep the stack of `inorderIter` as a member and make `pushLeft` the only loop:

```cpp
class InorderIterator {
public:
    explicit InorderIterator(const Node* root) { pushLeft(root); }
    bool hasNext() const { return !st_.empty(); }
    int next() {
        const Node* n = st_.back();
        st_.pop_back();
        pushLeft(n->right);
        return n->val;
    }

private:
    void pushLeft(const Node* n) {
        for (; n; n = n->left) st_.push_back(n);
    }
    std::vector<const Node*> st_;
};
```

`next` pops the leftmost unvisited node, then pushes the left spine of its right subtree, which is the work the loop of `inorderIter` would do before the next visit. The constructor costs `O(h)`. A single `next` costs `O(h)` in the worst case, but over a whole traversal every node is pushed once and popped once, so `next` is `O(1)` amortised, with `O(h)` memory. The counted pushes were exactly `n` on every shape; the largest cost of one `next` was 9 on the perfect tree of 1023 nodes and 17 on a random tree of 100,000 nodes. This is LeetCode 173, and the reason a BST iterator needs `O(h)` memory, not `O(n)`.

The point of laziness is the early exit. The kth smallest in a BST stops after `k` steps:

```cpp
inline int kthSmallest(const Node* root, int k) {      // 1 <= k <= n; stops after k steps
    InorderIterator it(root);
    while (k-- > 1) it.next();
    return it.next();
}
```

The cost is `O(h + k)`, not `O(n)`.

---

## 6. Morris traversal: a stack hidden in the null links

### 6.1 The idea

A tree with `n` nodes has `n + 1` null child pointers (tutorial 01, section 4.4). The stack of an inorder traversal exists to remember where to return after finishing a left subtree. That place is always the same node: the root of the subtree, `cur`. The last node of that left subtree in inorder is the **predecessor** of `cur`: the rightmost node of `cur->left`. Its right pointer is null, because it is the rightmost. So we write the return address there: for a while, that null pointer becomes a **thread** from the predecessor to `cur`.

On arriving at `cur` we do the following:

1. `cur` has no left child: visit it and go right (this follows a thread if one was put there).
2. `cur` has a left child. Find `pred`, the rightmost node of `cur->left`, stopping early if `pred->right == cur`.
3. If `pred->right` is null: this is the first arrival. Set `pred->right = cur` and go left.
4. If `pred->right == cur`: this is the second arrival, after the whole left subtree is done. Set `pred->right = nullptr` to undo the thread, visit `cur`, and go right.

Step 4 can recognise a thread because in an unmodified tree, a node in the left subtree of `cur` can never have `cur` as its right child. So the thread is its own marker, and the tree is restored by the time the traversal ends.

```cpp
template <class F>
void morrisInorder(Node* root, F visit) {
    Node* cur = root;
    while (cur) {
        if (!cur->left) {
            visit(cur->val);
            cur = cur->right;
            continue;
        }
        Node* pred = cur->left;                       // inorder predecessor of cur
        while (pred->right && pred->right != cur) pred = pred->right;
        if (!pred->right) {
            pred->right = cur;                        // thread: remember the way back
            cur = cur->left;
        } else {
            pred->right = nullptr;                    // second arrival: undo the thread
            visit(cur->val);
            cur = cur->right;
        }
    }
}

template <class F>
void morrisPreorder(Node* root, F visit) {
    Node* cur = root;
    while (cur) {
        if (!cur->left) {
            visit(cur->val);
            cur = cur->right;
            continue;
        }
        Node* pred = cur->left;
        while (pred->right && pred->right != cur) pred = pred->right;
        if (!pred->right) {
            visit(cur->val);                          // first arrival: visit now
            pred->right = cur;
            cur = cur->left;
        } else {
            pred->right = nullptr;
            cur = cur->right;
        }
    }
}
```

Events of `morrisInorder` on T, in order (a thread `a->b` means that the right pointer of `a` now points to `b`):

| Event | Why |
|---|---|
| thread 4->1 | the root has a left child; the rightmost node under 2 is 4 |
| thread 3->2 | node 2 has a left child; its predecessor is 3 |
| visit 3 | node 3 has no left child; go right, which is the thread to 2 |
| unthread 3, visit 2 | second arrival at 2 |
| thread 5->4 | node 4 has a left child 5 |
| visit 5 | go right, which is the thread to 4 |
| unthread 5, visit 4 | second arrival at 4; go right, which is the thread to 1 |
| unthread 4, visit 1 | second arrival at the root |
| visit 6, visit 7 | no left children |

**Preorder** needs one change: visit `cur` at the first arrival, when the thread is created, instead of the second. That is `morrisPreorder` above.

### 6.2 Cost

The time is `O(n)`, although the search for `pred` looks like a loop inside a loop. Each search walks the right spine of one left subtree; a node lies on the right spine of the left child of at most one ancestor, and each search is done twice (create and undo), so the searches take at most `2n` steps in all. An instrumented copy of the loop counted 0 steps on both chains of 1000 nodes, 1004 on the perfect tree of 1023 nodes, 1022 on a random tree of 1000 nodes and 100,138 on a random tree of 100,000 nodes: about `n` or less. One thread is created per node that has a left child (999 for the left-leaning chain). At most `h - 1` threads are alive at once, and there are `n + 1` null links to hold them.

### 6.3 Postorder

Postorder cannot visit at the first or second arrival, so the code does something different. At the second arrival at `cur`, the whole left subtree has been walked. The part of its postorder that is still missing is its **right edge**: the path from `cur->left` going right down to `pred`, visited from the bottom to the top (each of these nodes finishes after its right child). Everything hanging to the left of that edge was emitted at earlier second arrivals. So at that moment we reverse the right edge, visit it, and reverse it back. A dummy node above the root makes the root's own right edge come out at the end.

```cpp
inline void reverseRight(Node* from, Node* to) {      // reverse the right-chain from..to
    if (from == to) return;
    Node* prev = nullptr;
    Node* cur = from;
    while (true) {
        Node* next = cur->right;
        cur->right = prev;
        prev = cur;
        cur = next;
        if (prev == to) break;
    }
}

template <class F>
void emitReversed(Node* from, Node* to, F& visit) {   // visit to, ..., from
    reverseRight(from, to);
    for (Node* p = to;; p = p->right) {
        visit(p->val);
        if (p == from) break;
    }
    reverseRight(to, from);                           // put the chain back
}

template <class F>
void morrisPostorder(Node* root, F visit) {
    Node dummy{0};                                    // gives the root a left-child slot
    dummy.left = root;
    Node* cur = &dummy;
    while (cur) {
        if (!cur->left) {
            cur = cur->right;
            continue;
        }
        Node* pred = cur->left;
        while (pred->right && pred->right != cur) pred = pred->right;
        if (!pred->right) {
            pred->right = cur;
            cur = cur->left;
        } else {
            pred->right = nullptr;
            emitReversed(cur->left, pred, visit);     // the right edge of cur's left subtree
            cur = cur->right;
        }
    }
}
```

On T, the groups emitted at the four second arrivals are `3`, `5`, `4 2` and `7 6 1`, which read together give the postorder 3 5 4 2 7 6 1. The reversal touches each edge a constant number of times, so the time is `O(n)` and the extra space `O(1)`.

### 6.4 The price

- **It writes to the tree.** The tree must be mutable and nobody else may read it during the traversal: another thread, or the same code, would see threads and loop or skip nodes. A `const` tree cannot be walked with it.
- **No early exit.** If you stop in the middle, the threads stay in the tree. I checked this on a left-leaning chain of three nodes: stopping after the first visit leaves two threads, and the child pointers then form a cycle (node 1, its left child node 2, whose right pointer is the thread back to node 1), so a later recursive walk or a destructor never ends. A later Morris walk of the damaged tree reports the wrong order. So the kth smallest by Morris must run to the end:

```cpp
inline int kthSmallestMorris(Node* root, int k) {
    int ans = -1;
    morrisInorder(root, [&](int v) { if (--k == 0) ans = v; });   // must run to the end
    return ans;
}
```

  This is `O(n)` for any `k`, where the iterator of section 5 is `O(h + k)`.
- **A throwing callback leaves the tree threaded** too. Use `noexcept` visitors or finish the repair in a destructor.
- **It is slower than a stack.** See section 8.

Morris published this method in 1979, answering a question Knuth had posed in 1968 about walking a tree with no stack and no modification (see Sources). Morris's version modifies the tree temporarily; the parent-link method of the next section does not.

---

## 7. Parent links: no stack, no writes

If every node stores a pointer to its parent, the next node in order can be found from the node alone:

```cpp
struct PNode {
    int val;
    PNode* left = nullptr;
    PNode* right = nullptr;
    PNode* parent = nullptr;
};

inline PNode* leftmost(PNode* n) {
    while (n && n->left) n = n->left;
    return n;
}

inline PNode* successor(PNode* n) {
    if (n->right) return leftmost(n->right);          // next is the leftmost of the right subtree
    while (n->parent && n == n->parent->right) n = n->parent;   // climb while we are a right child
    return n->parent;                                 // nullptr after the last node
}

template <class F>
void inorderParent(PNode* root, F visit) {
    for (PNode* n = leftmost(root); n; n = successor(n)) visit(n->val);
}
```

`successor` has two cases. If the node has a right subtree, the next node is its leftmost node. Otherwise climb while you are a right child (your parent was already visited), and the first parent reached from a left child is the answer; if the climb ends at the root, the node was the last. Each tree edge is walked down once and up once over the whole traversal, so a full walk takes exactly `2(n - 1)` edge moves: I counted 1998 for every chain of 1000 nodes and 2044 for the perfect tree of 1023 nodes (`2 * 1022`), 199,998 for the random tree of 100,000 nodes. A single `successor` call costs `O(h)` at worst and `O(1)` amortised over a traversal.

The cost is one pointer per node, kept correct on every insert, delete and rotation. It pays off where a node must be navigated with no cursor object: the declaration `rb_next(const struct rb_node *)` in the kernel's `rbtree.h` takes only the node, and the header defines `rb_parent`, which masks the low two bits off a combined parent-and-colour field (both checked, see Sources). So `rb_next` has no stack to use and must work from the node and its parent; I did not read its body in `lib/rbtree.c`. Topic 13 returns to this.

---

## 8. Comparing the five methods

Peak stack size (nodes or frames) for each explicit-stack version, measured with instrumented copies:

| Shape | `n` | `h` | `inorderIter` | `preorderPush` | `postorderLast` | `postorderReverse` | `walk` | `walkFlag` In | `walkFlag` Post |
|---|---|---|---|---|---|---|---|---|---|
| Left chain | 1000 | 1000 | 1000 | 1 | 1000 | 1 (+ output 1000) | 1000 | 2001 | 2001 |
| Right chain | 1000 | 1000 | 1 | 1 | 1000 | 1 (+ output 1000) | 1000 | 3 | 1002 |
| Perfect | 1023 | 10 | 10 | 10 | 10 | 10 (+ output 1023) | 10 | 21 | 21 |
| Random shape | 1000 | 23 | 14 | 14 | 23 | 12 (+ output 1000) | 23 | 29 | 38 |

`walk` always peaks at `h`. `walkFlag` needs up to about `2h`: a node is pushed twice (expand, then visit), and null children are pushed too.

Time for one full inorder traversal that sums the values, on a perfect tree of 1,048,575 nodes (20 levels), best of 9 runs, in milliseconds. The three rows are three memory layouts of the same tree: nodes in heap order (parent `i`, children `2i+1`, `2i+2`), nodes in random addresses, and nodes in inorder (so a traversal reads memory in order):

| Layout | Recursive | `inorderIter` | `walk` | Morris | Parent links |
|---|---|---|---|---|---|
| Heap order | 1.61 | 2.83 | 13.42 | 5.70 | 18.00 |
| Random addresses | 15.57 | 18.10 | 144.72 | 31.10 | 169.13 |
| Inorder | 4.34 | 4.82 | 18.32 | 8.83 | 7.06 |

Repeating the run gave the same picture within about 10 to 20 percent (106 to 163 ms for `walk` on random addresses). On this machine and these trees:

- Recursion and the explicit stack are close: the stack version took 1.1 to 1.8 times as long. The call stack is not slower than a vector.
- Morris took 2.0 to 3.5 times as long as recursion. It does more pointer work: two predecessor searches and two pointer writes for every node that has a left child.
- `walk` took 3.8 to 8.0 times as long as the stack version, and the parent-link version 6.4 and 9.3 times on the heap-order and random layouts but only 1.5 times on the inorder layout. I did not profile why. The layouts also did not behave as "sequential memory is fastest" would suggest (heap order was the fastest), and I did not investigate that either.
- Random addresses cost 6.4 times (stack) to 9.7 times (recursive) as much as heap order. The layout of the nodes in memory matters more than the choice between recursion and a stack.

| Method | Extra space | Writes | Early exit | Needs parent | Use when |
|---|---|---|---|---|---|
| Recursion | `O(h)` call stack | no | yes | no | depth is bounded |
| Explicit stack | `O(h)` heap | no | yes | no | the default for deep trees |
| Iterator | `O(h)` | no | yes, lazy | no | one value at a time |
| Morris | `O(1)` | temporary | no | no | no memory for a stack |
| Parent links | `O(1)` | no | yes | yes | the structure has them |

---

## 9. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Postorder without `last`, testing only `top->right` | The loop re-enters the right child forever; the stack grows | Compare with `last` (section 3) |
| Pushing the left child before the right one in `preorderPush` | The siblings come out swapped | Push right first |
| Using a reference to `st.back()` after `push_back` | Dangling reference (reallocation) | Copy what you need first (section 4) |
| `while (pred->right)` in Morris | Infinite loop on the second arrival, because the thread is a right pointer | Stop at `pred->right == cur` |
| Returning from the middle of a Morris loop | The tree keeps its threads: later recursive walks or the destructor never end, a later Morris walk is wrong | Always finish the loop (section 6.4) |
| Morris on a tree that other threads read, or on a `const` tree | Readers see threads; does not compile for `const` | Use a stack or parent links |
| Morris postorder without the dummy node | The right edge of the whole tree is never emitted | Hang the root under a dummy |

---

## 10. Questions to answer out loud

1. Why does a recursive traversal use `O(h)` memory, and when is `h` close to `n`?
2. Write inorder with an explicit stack. What does the stack contain? What is the one-line change for preorder?
3. Why is postorder harder with a stack, and what are the two fixes? Which one uses `O(n)` extra memory?
4. What are the three states of a frame in the one-loop version, and which recursive call does each correspond to?
5. Why is the BST iterator `O(1)` amortised per `next` but `O(h)` in the worst case?
6. Explain Morris traversal: where is the thread stored, and how does the algorithm know it has arrived a second time?
7. Why is Morris `O(n)` although it searches for the predecessor at every node?
8. Why can Morris not stop early, and what happens if you try?
9. Which change makes Morris produce preorder? What does postorder need?
10. How does `successor` find the next node with parent links, and why is a full walk `2(n - 1)` edge moves?
11. Which of the five methods would you choose on a 4 KiB task stack, and which for a tree shared by several threads?

---

## 11. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 11.1 On paper

**Problem A.** Run `preorderPush` on T and write the stack (bottom to top) after each visit.

*Where:* paper. *Goal:* seven stack contents. *How:* pop, visit, push the right child, then the left.

**Problem B.** Run `postorderLast` on T. For each visit write the stack before the pop and the value of `last`.

*Where:* paper. *Goal:* a table like section 3 (check yours against it only after writing it). *How:* push the left spine, then look at the top.

**Problem C.** Run `morrisInorder` on the tree U: root 4, with left child 2 (children 1 and 3) and right child 5. List the events.

*Where:* paper. *Goal:* thread and unthread events and the visits. *How:* the predecessor of 4 is 3; of 2 it is 1.

**Problem D.** Run `morrisPostorder` on T and list the groups that `emitReversed` produces.

*Where:* paper. *Goal:* four groups. *How:* at each second arrival, read the right edge of the left subtree from the bottom up.

**Problem E.** For a left-leaning chain of 5 nodes, how many threads are alive at the same time in `morrisInorder`, and how many null links does the tree have?

*Where:* paper. *Goal:* two numbers. *How:* every node except the last has a left child and a thread is created before going down.

**Problem F.** A BST is a left-leaning chain of one million nodes. Compare the kth smallest (`k = 1`) by `kthSmallest` and by `kthSmallestMorris`: time and extra memory.

*Where:* paper. *Goal:* two cost pairs. *How:* the constructor of the iterator pushes the whole left spine.

### 11.2 In your editor

For each, write the recursive version first, then the iterative one, and compare on random trees and on a chain of a million nodes.

1. **Binary Tree Preorder Traversal (144), Binary Tree Inorder Traversal (94), Binary Tree Postorder Traversal (145).** Where: LeetCode. Goal: the three lists without recursion. How: sections 2 and 3, then again with Morris.
2. **Binary Search Tree Iterator (173).** Where: LeetCode. Goal: `next` and `hasNext` with `O(h)` memory. How: the iterator of section 5.
3. **Kth Smallest Element in a BST (230).** Where: LeetCode. Goal: the kth value. How: `kthSmallest`; then think about the follow-up where the tree is modified often.
4. **Validate Binary Search Tree (98).** Where: LeetCode. Goal: true or false. How: iterative inorder, compare each value with the previous one; test with equal values and the smallest `int`.
5. **Flatten Binary Tree to Linked List (114).** Where: LeetCode. Goal: the tree rewritten as a right-leaning list in preorder, in place. How: the predecessor idea of Morris: hang the right subtree under the rightmost node of the left subtree, move the left subtree to the right, and continue.
6. **Recover Binary Search Tree (99).** Where: LeetCode. Goal: fix two swapped nodes. How: inorder with a previous pointer finds the two out-of-order places; try the `O(1)` space version with Morris, which the statement asks for as a follow-up (from memory).
7. **Parent-link predecessor.** Where: your editor. Goal: `predecessor` and a reverse walk. How: swap left and right everywhere in `successor`.

---

## Answers

**Problem A.** Stack after each visit: 1: `6 2`; 2: `6 4 3`; 3: `6 4`; 4: `6 5`; 5: `6`; 6: `7`; 7: empty. The visits are 1 2 3 4 5 6 7.

**Problem B.** The table in section 3 is this answer: visits 3 5 4 2 7 6 1, with the stacks and `last` values listed there.

**Problem C.** Thread 3->4, thread 1->2, visit 1, unthread 1 and visit 2, visit 3 (its right pointer is the thread to 4, so the next arrival is at 4), unthread 3 and visit 4, visit 5. The order is 1 2 3 4 5; the events come from the Python model, and the C++ code gives the same order.

**Problem D.** The groups are `3`, `5`, `4 2` and `7 6 1`, so the output is 3 5 4 2 7 6 1.

**Problem E.** Thread 2->1, 3->2, 4->3 and 5->4 are all created before the first visit, so 4 threads are alive at once (`n - 1 = h - 1`). The tree has `n + 1 = 6` null links: 4 of them become threads at the peak.

**Problem F.** The iterator's constructor pushes all one million nodes of the left spine, so it is `O(n)` time and `O(n)` memory, and the single `next` is cheap. Morris takes `O(n)` time and `O(1)` memory, with no early exit. On a balanced tree the iterator would be `O(h + k)` and Morris `O(n)`, but on this chain Morris wins on memory and ties on time.

**Problems 11.2.** All seven follow the listings. For 114: for each node with a left child, find the rightmost node of the left subtree, point its right pointer at the node's right child, move the left subtree to the right and clear the left pointer. For 98 the inorder values must be strictly increasing: compare with the previous value. For 99 the two swapped values show up as one or two places where the previous value is larger than the current one.

---

## Cheat sheet

- Recursion costs one frame per level: 32 bytes at `-O0` and about 18 bytes at `-O2` for the small function measured. Deep means a chain from sorted input, or a small stack.
- Inorder: push the left spine, pop and visit, go right. Preorder: visit at the push. Postorder: use `last`, or reverse "root, right, left" (which buffers `n` values).
- One loop for all orders: a state per frame (0 going left, 1 back from the left, 2 back from the right). The flag idiom pushes in the reverse of the wanted order.
- Iterator: `pushLeft` in the constructor and after each `next`; `O(1)` amortised, `O(h)` memory, early exit.
- Morris: thread the predecessor's right pointer to `cur`; the second arrival is seen when `pred->right == cur`; unthread, visit, go right. Preorder visits at the first arrival. Postorder reverses the right edge, with a dummy root. `O(n)` time, `O(1)` space, tree temporarily modified, no early exit.
- Parent links: `successor` is the leftmost of the right subtree, or climb while a right child; a full walk is `2(n - 1)` edge moves.
- Measured here: the stack took 1.1 to 1.8 times as long as recursion, Morris 2.0 to 3.5 times; node layout in memory mattered more than the method.

---

## Sources

- Titles and numbers of 94, 98, 99, 114, 144, 145, 173 and 230 were checked against the [talentd.in binary tree problem list](https://www.talentd.in/fleetcode/problems?topic=binary-tree). Difficulty labels are not used because I could not verify them.
- [Wikipedia, "Threaded binary tree"](https://en.wikipedia.org/wiki/Threaded_binary_tree): it says that Knuth's 1968 textbook asked whether a non-recursive inorder traversal with no stack that leaves the tree unmodified exists, that Morris presented tree threading in 1979 as one solution, and cites Morris, "Traversing binary trees simply and cheaply", *Information Processing Letters*. I did not read the paper.
- [LWN, "[RFC 2/2] x86_64: expand kernel stack to 16K" (2014)](https://lwn.net/Articles/600645/): stack overflows on an 8 KiB x86-64 kernel stack under memory pressure, the proposal to use 16 KiB, and arm64 already using 16 KiB. The post does not record the outcome.
- [`include/linux/rbtree.h` (mirror)](https://git.zx2c4.com/linux-rng/tree/include/linux/rbtree.h?id=185a4caeecabc150106deda1da170b09f2ad803f): the declaration of `rb_next(const struct rb_node *)`, `rb_first`, `rb_last` and the `rb_parent` macro. The body of `rb_next` was not read.
- All other claims in this tutorial come from the tests and measurements described at the top.
