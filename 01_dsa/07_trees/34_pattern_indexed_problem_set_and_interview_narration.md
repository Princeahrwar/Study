# DSA 07.34: Pattern-Indexed Problem Set and Interview Narration

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (2 reading, 6 drilling the problem lists out loud) · You need: the tree topics you want to drill, above all [Binary tree fundamentals](01_binary_tree_fundamentals.md), [Iterative traversals](02_iterative_traversals_and_morris.md), [Bottom-up recursion](04_bottom_up_recursion.md), [Level and view problems](05_level_view_and_width_problems.md), [Paths, ancestors and LCA](06_paths_ancestors_and_lca.md) and [BST problem patterns](08_bst_problem_patterns.md). There is no companion demo file: the seven listings are in this file and are tested code.*

This is the last topic of the Trees area, and it adds no new structure. It does two things. First, it indexes the problems of the whole area by **the way information flows through the tree**, because that is what decides the solution: a problem is rarely "a tree problem", it is "answer built from the subtrees", or "state carried down", or "walk by levels". Second, it gives you a script for **saying** the solution out loud, because in an interview the code is half the answer; the other half is the conventions you fix, the shape you assume, the depth you worry about and the cost you state.

By the end you can:

- name the pattern of a tree problem from its wording, and write its template from memory;
- write every bottom-up problem as one function (`fold`) plus a combine, and every "state going down" problem as an explicit stack, without recursion;
- say when recursion depth is a real risk, with a number, and what you would do about it;
- run the narration script for a problem you have not seen, including the questions to ask first and the follow-ups to expect;
- pick drills by pattern instead of by a difficulty label.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 69,605 checks passed under AddressSanitizer with UBSan (GCC 13.3, C++17). Every function was compared with a separate plain-recursion or brute-force version on thousands of random small trees: for example maximum path sum against every pair of nodes, house robber against every subset of nodes, LCA and distance against walks of the root paths, the width against explicit heap positions, validity against the recursive bounds check. The published examples of the named problems (as I remember them) were checked literally. Every function also ran on **chains of 1,000,000 nodes** to show it does not recurse. Then 61 hand-made mutants of the listings were run against the tests: **56 were caught and 5 are equivalent changes** (section 8). Problem numbers are LeetCode's, **from memory**: check a number before you rely on it.

**Timing.** Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`. Times are **run 1 / run 2**, best of 5, and a claim is made only when it holds in both; they are this machine's, not yours.

---

## 1. Ask how the information flows

Before you think about code, answer three questions about the answer you want at a node:

| Question | If yes | Pattern |
|---|---|---|
| Is it computed from the answers of the left and right subtrees? | information flows **up** | bottom-up fold (2) |
| Does it depend on what is above the node (a bound, a sum so far, a maximum so far)? | information flows **down** | top-down with state (3) |
| Does it need the state on the way down **and** an undo on the way up? | down with undo | enter and exit (4) |
| Is it about nodes at the same depth, or a left-to-right order of levels? | **across** | level walk (6) |
| Is it about a distance or a path that may go up and then down? | **any direction** | parent map, then a graph walk (11) |
| Is the tree a BST and the question about order? | use the **invariant** | inorder, bounds, pruning (8) |
| Do you have to build or transmit a tree? | uniqueness of the encoding | construction, serialization (9, 10) |
| Many queries on a fixed or changing tree? | leave the tree behind | Euler tour, segment tree, augmentation (12, 13) |

A problem often combines two: "diameter" is up (the answer for a node uses both heights) with a side result (the best so far); "path sum III" is down (running sum) with an undo.

## 2. The index

Problems are named by LeetCode number from memory and grouped by pattern, not by difficulty. Do the first three of a row to learn the template, then the rest to see its variations.

| # | Pattern | Cue in the statement | Template | Problems | Taught in |
|---|---|---|---|---|---|
| 1 | Traversal order | "return the inorder / preorder / postorder", "iterate" | explicit-stack loops | 94, 144, 145, 173, 331 | Topics 01, 02 |
| 2 | Bottom-up fold | "height", "balanced", "diameter", "maximum path", "count subtrees such that" | `fold` | 104, 110, 543, 124, 337, 226, 250, 687, 508, 979, 968, 1372 | Topic 04 |
| 3 | Top-down state | "root-to-leaf", "good node", "sum of numbers", "bound from ancestors" | stack of (node, state) | 112, 113, 129, 257, 1448, 988 | Topic 06 |
| 4 | Enter and exit | "any downward path", "all paths with property", prefix sums, backtracking | `dfs` | 437, 113, 257 | Topic 06 |
| 5 | Two trees at once | "same", "symmetric", "merge", "is a subtree of" | stack of pairs | 100, 101, 572, 617, 951 | Topic 04 |
| 6 | Level walk | "level", "row", "right side", "zigzag", "width", "next pointer" | `level_order` | 102, 103, 107, 199, 515, 637, 662, 116, 117, 314, 987 | Topic 05 |
| 7 | Ancestors, LCA | "lowest common ancestor", "deepest nodes" | parent map or fold | 236, 235, 1650, 1123, 865 | Topics 06, 25 |
| 8 | BST order and pruning | "BST", "kth", "range", "successor", "two keys swapped" | inorder, bounds | 98, 230, 285, 501, 530, 538, 653, 99, 938, 669, 450 | Topics 07, 08 |
| 9 | Construction | "build the tree from" two traversals, a sorted list, a preorder | index map, stack | 105, 106, 108, 109, 889, 1008, 654, 96, 95 | Topics 03, 08 |
| 10 | Serialization, canonical form | "serialize", "duplicate subtrees" | preorder with markers | 297, 449, 652, 428 | Topic 03 |
| 11 | Graph view | "distance", "k edges away", "time to spread" | `parents` + breadth-first | 863, 2385, 1740, 742 | Topics 06, 24 |
| 12 | Subtree and rerooting | "for every node, sum or count in its subtree or from it", "sum of distances" | Euler tour, tree DP | 1443, 834, 310 | Topics 24, 26 |
| 13 | Ordered-set and range structures | "count smaller after", "intervals", "skyline", "calendar" | Fenwick, segment tree, augmented tree | 315, 327, 218, 715, 729, 352 | Topics 20 to 23, 30 |
| 14 | Design with a tree | "autocomplete", "prefix", "median of a stream", "file system" | trie, heap | 208, 211, 212, 642, 295, 1166 | Topics 15 to 18 |
| 15 | O(1) extra space | "without a stack", "flatten in place" | Morris, threading | 94 and 99 (Morris), 114 | Topic 02 |

## 3. The templates

Seven short listings, all without recursion, all tested. The idea behind them: **a recursive solution is a contract**, "this function returns X for the subtree, and has side effect Y". The listings keep the contract and replace the call stack with a data structure you control.

### 3.1 Nodes, building a tree, and the two drivers

```cpp
struct Node { int v; Node *l = nullptr, *r = nullptr; };
struct Pool { std::deque<Node> d; Node* mk(int v) { d.push_back(Node{v}); return &d.back(); } };   // owns every node; deque keeps addresses stable
constexpr int NIL = INT_MIN;                                                                        // "null" in the level-order input format

Node* build(Pool& p, const std::vector<int>& a) {            // the usual level-order format: [3,9,20,null,null,15,7]
    if (a.empty() || a[0] == NIL) return nullptr;
    Node* root = p.mk(a[0]); std::queue<Node*> q; q.push(root); size_t i = 1;
    while (!q.empty() && i < a.size()) {
        Node* n = q.front(); q.pop();
        if (i < a.size() && a[i] != NIL) { n->l = p.mk(a[i]); q.push(n->l); } ++i;
        if (i < a.size() && a[i] != NIL) { n->r = p.mk(a[i]); q.push(n->r); } ++i;
    }
    return root;
}

// Pattern "bottom-up": the answer for a node is a function of the answers for its two subtrees.
// fold(root, z, f): z is the answer for an empty subtree, f(node, left, right) combines. No recursion, so a chain of 10^6 nodes is fine.
template <class T, class F> T fold(Node* root, T z, F f) {
    if (!root) return z;
    std::vector<Node*> order, st{root};                       // root, right, left order; reversed it is a post-order
    while (!st.empty()) { Node* n = st.back(); st.pop_back(); order.push_back(n); if (n->l) st.push_back(n->l); if (n->r) st.push_back(n->r); }
    std::vector<T> res;                                       // finished subtree answers; right is on top of left
    for (size_t i = order.size(); i-- > 0;) {
        Node* n = order[i];
        T R = z, L = z;
        if (n->r) { R = std::move(res.back()); res.pop_back(); }
        if (n->l) { L = std::move(res.back()); res.pop_back(); }
        res.push_back(f(n, std::move(L), std::move(R)));
    }
    return std::move(res.back());
}

// Pattern "state going down / undo on the way up": enter(n) runs before a node's subtree, exit(n) after it.
template <class E, class X> void dfs(Node* root, E enter, X exit) {
    std::vector<std::pair<Node*, bool>> st; if (root) st.push_back({root, false});
    while (!st.empty()) {
        auto [n, done] = st.back(); st.pop_back();
        if (done) { exit(n); continue; }
        enter(n); st.push_back({n, true}); if (n->r) st.push_back({n->r, false}); if (n->l) st.push_back({n->l, false});
    }
}
```

`fold` is the bottom-up pattern as a library: you give it the answer for an empty subtree (`z`) and a function of the node and the two child answers. It finds a post-order without recursion (the traversal root, right, left, reversed) and keeps a stack of finished answers, the right one on top of the left. `dfs` is the enter and exit pattern: the stack holds each node twice, once to go in and once to come out.

What it costs: `fold` keeps the whole traversal order, **O(n) extra memory** against O(h) for recursion, and it is about twice as slow (section 4). What it buys: no crash on a deep tree.

### 3.2 Bottom-up (pattern 2)

```cpp
// Each of these is one call of fold with a different combine.
int height(Node* t) { return fold<int>(t, 0, [](Node*, int L, int R) { return 1 + std::max(L, R); }); }       // levels; 0 for empty
int count_nodes(Node* t) { return fold<int>(t, 0, [](Node*, int L, int R) { return 1 + L + R; }); }
int diameter(Node* t) {                                                                                         // edges on the longest path
    using P = std::pair<int, int>;                                                                              // (height, best diameter below)
    return fold<P>(t, {0, 0}, [](Node*, P L, P R) { return P{1 + std::max(L.first, R.first), std::max({L.second, R.second, L.first + R.first})}; }).second;
}
bool is_balanced(Node* t) {                                                                                     // every node: heights differ by at most 1
    using P = std::pair<int, bool>;
    return fold<P>(t, {0, true}, [](Node*, P L, P R) { return P{1 + std::max(L.first, R.first), L.second && R.second && std::abs(L.first - R.first) <= 1}; }).second;
}
long long max_path_sum(Node* t) {                                                                               // best path, any start and end, at least one node
    using P = std::pair<long long, long long>;                                                                  // (best downward path from here, best path below)
    return fold<P>(t, {0, LLONG_MIN / 4}, [](Node* n, P L, P R) {
        long long l = std::max(0LL, L.first), r = std::max(0LL, R.first);
        return P{n->v + std::max(l, r), std::max({L.second, R.second, n->v + l + r})};
    }).second;
}
long long rob(Node* t) {                                                                                        // house robber III: no two adjacent nodes
    using P = std::pair<long long, long long>;                                                                  // (take this node, skip this node)
    P r = fold<P>(t, {0, 0}, [](Node* n, P L, P R) { return P{n->v + L.second + R.second, std::max(L.first, L.second) + std::max(R.first, R.second)}; });
    return std::max(r.first, r.second);
}
```

Everything here is `fold` with a different pair type. The habit worth learning is **choosing the pair**: the answer you return up (a height, a downward gain, "take" and "skip") is usually not the answer to the question, which is a second field you maximise on the way (the diameter, the best path, the max of take and skip). When a problem says "any path" and you return a single number, you have the wrong pair.

### 3.3 Top-down state and enter and exit (patterns 3 to 5)

```cpp
bool has_path_sum(Node* t, long long target) {                // root-to-leaf path with this sum; the remaining sum travels down
    std::vector<std::pair<Node*, long long>> st; if (t) st.push_back({t, target});
    while (!st.empty()) {
        auto [n, rem] = st.back(); st.pop_back(); rem -= n->v;
        if (!n->l && !n->r && rem == 0) return true;
        if (n->r) st.push_back({n->r, rem});
        if (n->l) st.push_back({n->l, rem});
    }
    return false;
}
int good_nodes(Node* t) {                                     // nodes not smaller than anything on their root path; the maximum travels down
    int c = 0; std::vector<std::pair<Node*, int>> st; if (t) st.push_back({t, t->v});
    while (!st.empty()) {
        auto [n, mx] = st.back(); st.pop_back(); if (n->v >= mx) ++c; mx = std::max(mx, n->v);
        if (n->r) st.push_back({n->r, mx});
        if (n->l) st.push_back({n->l, mx});
    }
    return c;
}
long long count_paths(Node* t, long long target) {            // downward paths (any start) with this sum: prefix sums along the current path
    std::unordered_map<long long, int> seen{{0, 1}}; long long cur = 0, ans = 0;
    dfs(t, [&](Node* n) { cur += n->v; auto it = seen.find(cur - target); if (it != seen.end()) ans += it->second; ++seen[cur]; },
           [&](Node* n) { --seen[cur]; cur -= n->v; });
    return ans;
}
bool is_symmetric(Node* t) {                                  // two nodes at a time: outer with outer, inner with inner
    if (!t) return true;
    std::vector<std::pair<Node*, Node*>> st{{t->l, t->r}};
    while (!st.empty()) {
        auto [a, b] = st.back(); st.pop_back();
        if (!a && !b) continue;
        if (!a || !b || a->v != b->v) return false;
        st.push_back({a->l, b->r}); st.push_back({a->r, b->l});
    }
    return true;
}
```

The state travels with the node on the stack (`rem`, `mx`) and each child gets its own copy: nothing to undo. When one shared structure is updated along the path (the prefix-sum map of `count_paths`), you need `dfs`: update on enter, undo on exit. Forgetting the undo is the classic bug (section 9).

### 3.4 Levels (pattern 6)

```cpp
std::vector<std::vector<int>> level_order(Node* t) {          // the queue holds one level at a time
    std::vector<std::vector<int>> out; std::vector<Node*> cur, nxt; if (t) cur.push_back(t);
    while (!cur.empty()) {
        out.emplace_back(); nxt.clear();
        for (Node* n : cur) { out.back().push_back(n->v); if (n->l) nxt.push_back(n->l); if (n->r) nxt.push_back(n->r); }
        cur.swap(nxt);
    }
    return out;
}
std::vector<int> right_view(Node* t) { std::vector<int> v; for (auto& lv : level_order(t)) v.push_back(lv.back()); return v; }
std::vector<std::vector<int>> zigzag(Node* t) { auto o = level_order(t); for (size_t i = 1; i < o.size(); i += 2) std::reverse(o[i].begin(), o[i].end()); return o; }
unsigned long long max_width(Node* t) {                       // distance between the leftmost and rightmost node of a level, counting gaps, as in a complete tree
    unsigned long long best = 0; std::vector<std::pair<Node*, unsigned long long>> cur, nxt; if (t) cur.push_back({t, 0});
    while (!cur.empty()) {
        best = std::max(best, cur.back().second - cur.front().second + 1); nxt.clear(); unsigned long long base = cur.front().second;
        for (auto& [n, i] : cur) { unsigned long long k = i - base; if (n->l) nxt.push_back({n->l, 2 * k}); if (n->r) nxt.push_back({n->r, 2 * k + 1}); }
        cur.swap(nxt);
    }
    return best;
}
```

The level walk keeps two vectors, this level and the next, which is clearer than a queue with a size counter and makes "last of the level" and "reverse every other level" one-liners. In `max_width` each node carries its index in a complete tree (`2k`, `2k + 1`). The unsigned arithmetic wraps on a very deep tree, but a **difference** of two wrapped values is still right, so the width is right while it fits in 64 bits.

### 3.5 BST (pattern 8) and building from a preorder (pattern 9)

```cpp
Node* lca_bst(Node* t, int p, int q) {                        // the first node whose value separates p and q
    while (t) { if (p < t->v && q < t->v) t = t->l; else if (p > t->v && q > t->v) t = t->r; else return t; }
    return nullptr;
}
bool is_valid_bst(Node* t) {                                  // inorder must be strictly increasing
    std::vector<Node*> st; long long prev = LLONG_MIN; Node* n = t;
    while (n || !st.empty()) {
        while (n) { st.push_back(n); n = n->l; }
        n = st.back(); st.pop_back(); if (n->v <= prev) return false; prev = n->v; n = n->r;
    }
    return true;
}
int kth_smallest(Node* t, int k) {                            // stop the inorder walk at the k-th node; k must be in range
    std::vector<Node*> st; Node* n = t;
    while (n || !st.empty()) {
        while (n) { st.push_back(n); n = n->l; }
        n = st.back(); st.pop_back(); if (--k == 0) return n->v; n = n->r;
    }
    return INT_MIN;
}
long long range_sum(Node* t, int lo, int hi) {                // prune what cannot hold a key in [lo, hi]
    long long s = 0; std::vector<Node*> st; if (t) st.push_back(t);
    while (!st.empty()) {
        Node* n = st.back(); st.pop_back();
        if (n->v >= lo && n->v <= hi) s += n->v;
        if (n->v > lo && n->l) st.push_back(n->l);
        if (n->v < hi && n->r) st.push_back(n->r);
    }
    return s;
}
Node* from_preorder(Pool& p, const std::vector<int>& a) {     // BST from its preorder (distinct keys) in O(n): the stack is the right spine
    if (a.empty()) return nullptr;
    Node* root = p.mk(a[0]); std::vector<Node*> st{root};
    for (size_t i = 1; i < a.size(); ++i) {
        Node* n = p.mk(a[i]); Node* last = nullptr;
        while (!st.empty() && st.back()->v < a[i]) { last = st.back(); st.pop_back(); }
        if (last) last->r = n; else st.back()->l = n;
        st.push_back(n);
    }
    return root;
}
```

`is_valid_bst` checks that the inorder is **strictly** increasing, with a `long long` previous value so a key of `INT_MIN` is legal; both are tested. `from_preorder` is the O(n) construction: the stack is the right spine, and a smaller key that arrives after bigger ones pops back to its parent.

### 3.6 Graph view (patterns 7 and 11)

```cpp
// Pattern "treat the tree as an undirected graph": give every node its parent, then walk in any direction.
using Parents = std::unordered_map<Node*, Node*>;
Parents parents(Node* t) {
    Parents P; std::vector<Node*> st; if (t) { st.push_back(t); P[t] = nullptr; }
    while (!st.empty()) { Node* n = st.back(); st.pop_back(); for (Node* c : {n->l, n->r}) if (c) { P[c] = n; st.push_back(c); } }
    return P;
}
Node* lca(Node* t, Node* p, Node* q) {                        // general binary tree: climb the deeper one, then both
    Parents P = parents(t);
    auto depth = [&](Node* n) { int d = 0; for (Node* x = n; P[x]; x = P[x]) ++d; return d; };
    int dp = depth(p), dq = depth(q);
    while (dp > dq) { p = P[p]; --dp; }
    while (dq > dp) { q = P[q]; --dq; }
    while (p != q) { p = P[p]; q = P[q]; }
    return p;
}
std::vector<int> at_distance(Node* t, Node* target, int k) {  // values exactly k edges from target, in any direction (breadth-first)
    Parents P = parents(t); std::unordered_map<Node*, bool> seen{{target, true}}; std::vector<Node*> cur{target}, nxt;
    for (int d = 0; d < k && !cur.empty(); ++d) {
        nxt.clear();
        for (Node* n : cur) for (Node* c : {n->l, n->r, P[n]}) if (c && !seen[c]) { seen[c] = true; nxt.push_back(c); }
        cur.swap(nxt);
    }
    std::vector<int> out; for (Node* n : cur) out.push_back(n->v);
    return out;
}
```

The tree has no parent pointers, so make them: one traversal, one map. After that "k edges away in any direction" is a breadth-first search, and LCA is "climb the deeper one, then both". If the interviewer says the nodes already have parent pointers (problem 1650), the first line disappears.

### 3.7 Serialization (pattern 10)

```cpp
// Preorder with a marker for null: unique for any binary tree. Both directions use an explicit stack.
std::string serialize(Node* t) {
    std::string s; std::vector<Node*> st{t};
    while (!st.empty()) {
        Node* n = st.back(); st.pop_back();
        if (!n) { s += "#,"; continue; }
        s += std::to_string(n->v) + ","; st.push_back(n->r); st.push_back(n->l);
    }
    return s;
}
Node* deserialize(Pool& p, const std::string& s) {
    std::vector<int> tok;
    for (size_t i = 0; i < s.size();) { size_t j = s.find(',', i); std::string w = s.substr(i, j - i); tok.push_back(w == "#" ? NIL : std::stoi(w)); i = j + 1; }
    if (tok.empty() || tok[0] == NIL) return nullptr;
    Node* root = p.mk(tok[0]); std::vector<std::pair<Node*, int>> st{{root, 0}}; size_t i = 1;      // (node, children filled so far)
    while (!st.empty() && i < tok.size()) {
        Node* n = st.back().first; int k = st.back().second++;
        if (k == 2) { st.pop_back(); continue; }
        int t = tok[i++];
        if (t != NIL) { Node* c = p.mk(t); (k == 0 ? n->l : n->r) = c; st.push_back({c, 0}); }
    }
    return root;
}
```

Preorder with a marker for null identifies any binary tree uniquely (a preorder without markers does not; inorder alone does not either). The same encoding as a map key finds duplicate subtrees (652).

## 4. Recursion depth, with a number

Recursion is fine when the depth is small and **known**. Whether it is known is a question about the input, not about the algorithm. Height of a left chain, in a child process with an 8 MB stack:

| Nodes in the chain | Recursive height | `fold` height |
|---|---|---|
| 10,000 | exit 0 | exit 0 |
| 100,000 | exit 0 | exit 0 |
| 1,000,000 | killed by signal 11 | exit 0 |

(`-11` is a kill by signal 11, a stack overflow.) At 10^5 the recursion survived and at 10^6 it did not: the frame of this function at `-O2` is therefore between about 8 and 80 bytes (8 MB divided by the two sizes), and the limit is this machine's, 8 MB being the Linux default. Judge it like this:

- **A balanced tree** (red-black, AVL, B-tree) has depth at most about 2 log2 n: recursion is safe.
- **A BST of random keys** has depth about 2 ln n on average; the deepest of the 10^6-key tree used below was 50.
- **A BST of sorted or adversarial keys, a parsed expression, a linked structure someone else built** can be a chain: recursion is a bug waiting for the input. Many online judges promise a small n (up to about 10^4 nodes, from memory) so the question never comes up there; a production service has no such promise.

The price of immunity, on a random BST of 10^6 nodes (ns per node, run 1 / run 2):

| Computation | Recursive | `fold` |
|---|---|---|
| Height | 24.3 / 24.2 | 51.7 / 52.5 |
| Diameter | 26.9 / 26.8 | 52.0 / 54.1 |

`fold` was about twice as slow in both runs, because of the extra vector. That is the trade to state aloud: "recursion is simplest and fastest; I use an explicit stack if the depth is not bounded." The middle path, an explicit stack that holds state instead of a precomputed order, is what `dfs` and the top-down listings do, at O(h) memory.

## 5. Narration: how to say it

**The script.** The same eight beats for any tree problem.

1. **Restate and example.** One sentence in your words, then the smallest example that is not trivial (three nodes, one of them with a single child).
2. **Fix the conventions.** Ask, do not assume:
   - How is the tree given: nodes with pointers, or an array (level-order with nulls)?
   - Can it be empty? Are values negative, repeated, or at the extremes of `int`?
   - If it is a BST: strict, or are duplicates allowed, and on which side?
   - What is a "path": must it start at the root, end at a leaf, go only downward?
   - What is the depth, in the worst case? (This is the recursion question.)
3. **Name the pattern by information flow.** "A node's answer comes from its children's, so this is bottom-up; I need to return a pair, the height for my parent and the best so far for the answer."
4. **State the contract of the helper** before writing it: what it returns for an empty subtree, what it returns for a node, and any side effect.
5. **Write it**, then trace the small example, saying the values at each node.
6. **State the cost.** Time O(n): each node is visited a constant number of times. Space O(h) for the stack of a recursion, or O(w) for a level walk, where **h is between log n and n**: say both ends.
7. **Test aloud:** empty, one node, a chain on each side, a perfect tree, duplicates, negatives, the extremes.
8. **Offer the follow-ups before they are asked:** the iterative version, O(1) extra space (Morris), what changes if the tree is balanced, if it is huge, or if it changes while you read it.

**Shape assumptions and what each one costs.**

| Assumption | If it holds | If it fails |
|---|---|---|
| Depth is O(log n) (balanced) | recursion is safe; O(h) space is O(log n) | O(n) stack: use iteration |
| Tree is a valid BST | O(h) search, inorder is sorted | a "BST" with duplicates needs a stated rule; validate with strict order |
| Nodes have parent pointers | LCA and distance without a map | build the map, O(n) extra |
| Tree is complete or perfect | index arithmetic, O(1) size by height | no shortcut |
| Values are positive | a path never gains from being shorter | clamp negative gains to 0 (`max_path_sum`) |
| The tree is read-only | any approach | Morris traversal rewires and must restore; unsafe with readers |

**Worked narration 1: diameter** (543, bottom-up). "The diameter is the longest path, counted in edges, between any two nodes. Is the path allowed to avoid the root? (Yes.) It is a bottom-up problem: for each node the longest path **through** it is the height of its left subtree plus the height of its right one, and the answer is the maximum over all nodes. So each subtree returns two numbers: its height, which its parent needs, and the best path seen inside it, which is the answer. For an empty subtree both are 0. For a node: height is 1 plus the larger child height; best is the largest of the two children's bests and the sum of the two heights. On the tree 3, 9, 20, 15, 7 the node 20 has heights 1 and 1, so a path of 2; the root has heights 1 and 2, so a path of 3: the answer. Time O(n), one visit per node. Space O(h) with recursion; if the tree can be a chain I would use an explicit post-order, which is O(n) extra memory but cannot overflow. Tests: empty is 0, a single node is 0, a chain of n is n-1, and the longest path does not pass through the root when one subtree is a deep zigzag."

**Worked narration 2: validate a BST** (98, top-down or inorder). "Is a BST strict? I will assume no duplicates are allowed. The common mistake is to compare each node only with its children; a node's whole left subtree must be smaller, so I need either bounds travelling down, or the property that the inorder is strictly increasing. I will use the inorder, with an explicit stack: remember the previous value, and fail if the current one is not larger. The previous value starts at the smallest `long long`, not `INT_MIN`, because a node may hold `INT_MIN` itself. Time O(n), space O(h) stack, and it stops at the first violation. The bounds version has the same cost and carries the interval (low, high) with each node; I would pick it if I also wanted the offending node." Then the follow-ups: "if equal keys are allowed on one side, a strictly increasing inorder is the wrong test; I would carry bounds with one closed end instead."

**Worked narration 3: when you do not know the pattern.** Say the three questions of section 1 aloud on a small example: "does the answer at this node depend only on the children, only on the ancestors, or both?" It is a legitimate thing to say, it buys thinking time, and it usually settles the pattern.

## 6. The published examples, as checked by the tests

| Problem | Input (level order) | Answer |
|---|---|---|
| Height, diameter | 3, 9, 20, null, null, 15, 7 | 3 levels, 3 edges |
| Level order, zigzag, right view | same | `[[3],[9,20],[15,7]]`, `[[3],[20,9],[15,7]]`, `[3,20,7]` |
| Maximum path sum (124) | -10, 9, 20, null, null, 15, 7 | 42 |
| House robber III (337) | 3, 2, 3, null, 3, null, 1 | 7 |
| Path sum III (437), target 8 | 10, 5, -3, 3, 2, null, 11, 3, -2, null, 1 | 3 |
| Validate BST (98) | 5, 1, 4, null, null, 3, 6 | false |
| Maximum width (662) | 1, 2, 3, 4, null, null, 5 | 4 |
| LCA of 5 and 1; of 5 and 4 (236) | 3, 5, 1, 6, 2, 0, 8, null, null, 7, 4 | 3; 5 |
| Nodes at distance 2 from 5 (863) | same | 1, 4, 7 |
| BST from preorder 8, 5, 1, 7, 10, 12 (1008) | | serialises to `8,5,1,#,#,7,#,#,10,#,12,#,#,` |

## 7. What a drill session looks like

Pick one pattern per session, not one difficulty. Do three problems of the row in the index with a timer: ten minutes to say the pattern, the contract and the cost aloud **before** typing, twenty to write and test. Then take one problem from the row **without** looking at the template and note which of the section 5 beats you skipped. Two patterns are worth drilling until they are automatic: the bottom-up pair (what does the child return, what is the answer) and enter and exit (what do I undo).

## 8. What the tests check, and what they cannot

Of the 61 mutants, the survivors that are genuinely equivalent are these, each with the reason:

- **Pushing the right child before the left in `dfs`:** the order of the two subtrees changes which path is visited first, not any path's prefix sums, so the counts are identical.
- **`max_width` without the per-level normalisation (`k = i`):** unsigned wrap-around cancels in the difference of two indices, so the width is unchanged while it fits in 64 bits (section 3.4).
- **`range_sum` pruning with `>=` and `<=` instead of `>` and `<`** (two mutants): the extra subtree it visits holds only keys outside the range; pruning changes the work, not the sum.
- **`from_preorder` popping on `<=` instead of `<`:** the keys are distinct, so the two tests agree.

## 9. Common mistakes

- Returning one number from a bottom-up helper when the answer needs two (diameter, maximum path, house robber).
- Counting the diameter in nodes when the statement says edges (or the reverse): one off, everywhere.
- Not clamping negative child gains to 0 in maximum path sum, and starting the best at 0 instead of minus infinity: an all-negative tree returns 0.
- Comparing a BST node only with its children; allowing equal keys without saying the rule; using `INT_MIN` as the initial bound.
- Forgetting the undo on exit in a shared structure (prefix-sum counts): paths from sibling subtrees leak into each other.
- Passing a copied state when the structure is shared, or a shared one when it must be copied: decide which, per variable.
- Width computed with raw indices and `int`: overflows at depth 32 on a right-leaning tree.
- Assuming a path goes downward when the statement allows up-and-down (diameter, LCA distance) or the reverse (path sum III).
- Using recursion on input that can be a chain, and saying nothing about it.
- Building from preorder plus inorder with duplicate values: the split is ambiguous; the problems promise distinct values, so say that you rely on it.

## 10. Questions to answer out loud

1. **How do you pick the pattern of a tree problem?** By how the answer at a node depends on the rest: on the subtrees (bottom-up), on the ancestors (top-down), on both with undo (enter and exit), on the same depth (level walk), on any direction (graph view).
2. **What does `fold` need from you?** The answer for an empty subtree and a combine of the node with its two child answers.
3. **Why does a bottom-up helper often return a pair?** The value the parent needs (a height, a gain) is not the value asked for (a diameter, a best path); the second rides along.
4. **When is recursion a bug?** When the depth is not bounded by something you control: a chain of 10^6 nodes overflowed an 8 MB stack here.
5. **What does an explicit stack cost?** Nothing in time order, a constant factor in practice (about 2 here for the order-array fold), and memory O(h) with state on the stack or O(n) with a precomputed order.
6. **How do you validate a BST?** Strictly increasing inorder with a wide enough previous value, or bounds carried down; never parent against child only.
7. **How do you find nodes k edges from a given node?** Give every node its parent, then breadth-first search from the node over left, right and parent.
8. **What identifies a binary tree uniquely as a string?** Preorder with a marker for each null; a preorder alone, or an inorder alone, does not.
9. **What do you say about complexity?** Time O(n); space O(h) for recursion or a stack, O(w) for levels, and h is between log n and n.
10. **What do you do when you are stuck?** Say the three questions aloud on the three-node example, then write the contract of the helper before any code.

## 11. Problems

**A.** Maximum path sum on the tree 2, -1 and the tree -3: give both answers and say which line of `max_path_sum` produces each.

**B.** The tree has n nodes and is a chain. State the time and space of each `fold` listing and of its recursive twin, and what happens to each at n = 10^6 on an 8 MB stack.

**C.** Write the contract, in one sentence each, for the helpers of: (a) balanced, (b) path sum III, (c) LCA of a general tree.

**D.** Choose the pattern: (a) the sum of all left leaves; (b) the number of nodes equal to the average of their subtree; (c) the time for a fire starting at a node to burn the whole tree; (d) the k-th smallest of a BST that changes between queries; (e) the vertical order of nodes.

**E.** `count_paths` forgets its undo (`cur` is restored but `seen` is not). Give a 3-node tree on which it returns a wrong answer and trace it.

**F.** `max_width` is written with `int` indices and no normalisation. At which depth of a right-leaning chain does the index of the last node overflow?

**G.** Narrate, in the eight beats, the problem "invert a binary tree" (226) in under two minutes, including the follow-up about a chain of 10^6 nodes.

**H.** The interviewer says "now the tree is huge and does not fit in memory". For `height`, `kth_smallest` and `serialize`, say what changes.

### Answers

**A.** 2 + -1: the best path for the tree 2, -1 is just the node 2 (the child's gain is clamped to 0): the `std::max(0LL, L.first)` line, answer 2. The single node -3 gives -3: the start value `LLONG_MIN / 4` for the best (not 0) and the `n->v + l + r` term with both clamps 0.

**B.** `fold`: time O(n), space O(n) (the order vector plus the result stack), safe at 10^6. Recursive twin: time O(n), space O(h) = O(n), and at 10^6 on an 8 MB stack it is killed (measured, section 4). The top-down listings (`has_path_sum` and the others with a stack of states): O(n) time, O(h) space on the heap, safe.

**C.** (a) Returns (height, whether balanced) of the subtree, (0, true) for empty. (b) `dfs`: on enter, adds the node to the running sum and counts earlier prefixes equal to sum minus target; on exit, removes it. (c) Returns the node where p and q meet in this subtree, or nothing; with parent pointers, climb the deeper one then both.

**D.** (a) Top-down state (is this node a left child?), or bottom-up with a flag. (b) Bottom-up pair (sum, count) per subtree. (c) Graph view: parents, then breadth-first from the start node; the answer is the last distance. (d) Ordered-set structure: an order-statistic tree or a Fenwick tree over keys (Topics 20 and 30), not a traversal per query. (e) Level walk with a column index (987 sorts ties by row then value).

**E.** Tree: root 1 with left child 0 and right child 1, target 1. The right answer is 3: the root alone, the root with its left child (1 + 0), and the right child alone. Trace without the undo: entering the root, cur = 1 finds the prefix 0 once (count 1) and records prefix 1. Entering the left child, cur = 1 finds prefix 0 again (count 2) and records prefix 1 a second time. On exit the left child's prefix is **not** removed. Entering the right child, cur = 2 looks for prefix 1 and finds it twice, one of them left over from the left child, so the answer is 4. The undo restores the map to the state at the parent.

**F.** On a right-leaning chain the node at depth d has index 2^d - 1, which is `INT_MAX` at depth 31 and overflows at depth 32. Normalising per level (subtract the first index of the level) keeps the indices small here, because each level has one node; a tree that is wide at a deep level still needs 64 bits and a width that fits.

**G.** "Swap the two children of every node. Empty tree: return it. It is bottom-up or top-down, either works; the order does not matter because each node's swap is independent. Iteratively: a stack, pop a node, swap its children, push both. Time O(n), space O(h) for the stack of a skewed tree, O(w) at worst. A chain of 10^6 nodes would overflow the recursion, so I use the stack. Tests: empty, a single node, a chain, a perfect tree; inverting twice gives the original."

**H.** `height`: a streaming post-order over the stored nodes needs the stack, so depth still matters; an external-memory tree is processed level by level. `kth_smallest`: do not traverse; keep subtree sizes in the nodes (Topic 30) or use an index (a B-tree with counts, Topic 29). `serialize`: stream the preorder to a file with a stack; the format is already sequential, which is why it is the format of choice.

---

## Cheat sheet

- **Ask first:** does the answer depend on subtrees (up), ancestors (down), both with undo, the same depth (across), or any direction (graph)?
- **Up:** `fold(t, z, combine)`; return the pair (what the parent needs, the best so far). **Down:** stack of (node, state). **Undo:** `dfs(enter, exit)`. **Across:** two vectors per level. **Any direction:** `parents`, then breadth-first.
- **BST:** strictly increasing inorder with a `long long` previous; bounds if you need the offender; O(n) build from a preorder with the right-spine stack.
- **Narration:** restate, conventions, pattern by flow, contract, code and trace, cost (h between log n and n), tests, follow-ups.
- **Depth:** a 10^6 chain killed the recursive height on an 8 MB stack (10^5 survived); `fold` was about twice as slow on a random BST and never crashes.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`). The code is my own implementation, tested as described in the opening.
- Problem numbers and titles are LeetCode's, from memory, not rechecked in this session; the published examples in section 6 are as I remember them and the listed answers are the ones the tests compute and compare with independent references. Notes of "about 10^4 nodes" for typical judge limits are from memory.
- The Morris traversal, parent-pointer, rerooting, Euler-tour and augmented-tree patterns are taught in Topics 02, 06, 24 to 26 and 30; this topic only indexes them.
