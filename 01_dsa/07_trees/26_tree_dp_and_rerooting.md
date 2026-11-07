# DSA 07.26: Tree DP and Rerooting

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [General trees and the Euler tour](24_general_trees_euler_tour_and_subtree_queries.md) (this file uses its `RootedTree`: `parent`, `order`, `size` and the children lists `cstart` / `clist`) and the [recursion and divide and conquer](../06_recursion_and_backtracking/04_divide_and_conquer_and_recurrences.md) habit of "solve the parts, then combine". [LCA at scale](25_lca_at_scale.md) is not needed. There is no companion demo file: every structure is listed in this file and every listing is tested code.*

A **tree DP** computes, for every node, a small table that describes its subtree, by combining the tables of its children. Because a tree has no cycles, the children's subtrees never overlap, so their tables can be combined independently, and the whole job is one pass over the nodes in O(n). It is the first dynamic-programming tool for graphs, and it is the answer to a large family of interview questions that begin "given a tree, choose some nodes or edges such that...".

**Rerooting** answers the follow-up "...and now do it for every possible root". Done naively that is n runs of O(n); rerooting finishes all n answers in O(n) total, by passing information down the tree as well as up.

By the end of this tutorial you can:

- write any subtree DP as a loop over `order` backwards, with no recursion, so that a path of 10^6 nodes cannot overflow the stack;
- solve **maximum weight independent set** (with reconstruction of the chosen nodes), **counting independent sets**, **maximum matching** and **minimum vertex cover**, **list colourings** and **maximum path sum with negative values**, and say what state each one keeps per node;
- explain the **rerooting** technique with `down` and `up` messages, why it uses prefix and suffix merges and not an inverse, and write a new rerooting problem by supplying four small functions;
- get **sum of distances**, **eccentricity**, **centers** and **centroids** for every node in one pass, and cross-check them against the diameter, the centers and the centroids of Topic 24;
- choose between a single DP, rerooting, and the O(1) shortcut that exists when the merge has an inverse.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 177,788 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: independent set against subset enumeration on every tree of 1 to 16 nodes in seven shapes (random, path, star, caterpillar, binary heap, broom, deep random), with random labels and random roots and with negative weights; the counts against enumeration for the moduli 1, 7, 1009 and 10^9 + 7; the matching against every subset of edges (and its size against the brute-force minimum vertex cover, which is König's theorem on a tree); list colourings against all k^n assignments for n up to 8; the closed form k(k - 1)^(n - 1) for proper colourings on trees up to 1000 nodes; the maximum path sum against all pairs of nodes (including all-negative values); the three rerooting problems against a breadth-first search from every node for every tree of 1 to 40 nodes, with the same answers from a second root and agreement with the diameter, centers and centroids of Topic 24; a path and a star of 300,000 nodes (where the answers are known by formula); the tie-breaking rule on a two-node tree and on all-zero weights; and bad arguments for every function that checks its arguments. Finally 85 hand-made mutants of the code were run against the tests (a flipped comparison, a lost `% mod`, a loop that stops one early, a swapped prefix and suffix and so on): **80 were caught and 5 are equivalent changes** that do not alter any final output (listed after the common mistakes).

**Data and timing.** The trees are **synthetic**, 1,000,000 nodes, labels shuffled, rooted at the node labelled 0. "Random" attaches each node to a uniformly random earlier node, "path" is one chain, "star" is one centre with 999,999 leaves. Independent-set weights are random in 0 to 999 and path values in -1000 to 1000. Times are milliseconds on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 25), best of 3 repetitions of the DP only (building the `RootedTree` is not included), shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice and about results I did not test are from general knowledge and say so.

---

## 1. The pattern: a loop over `order`, backwards

Topic 24's `RootedTree` stores `order`, the nodes in DFS preorder from the root. In preorder every node comes **before** all of its descendants. So walking `order` from the end to the start visits every node after all of its children: when the loop reaches `v`, its table is final, and it can be pushed into its parent. Walking from the start to the end visits every node after its parent, which is what a top-down pass needs (reconstruction, and the `up` messages of rerooting).

```text
for i = n-1 down to 1:        // children before parents
    v = order[i];  p = parent[v]
    combine(table[p], table[v])
answer = table[root]
```

There is no recursion, so there is no stack to overflow: a recursive DFS on a path of 10^6 nodes needs 10^6 frames, which is more than the default 8 MiB stack allows (general knowledge about typical Linux limits; the code here never has the problem). The same loop shape is used by every function below. What changes is only **what is stored per node** (the state) and **how a child is folded into its parent** (the transition).

How to design a state, in four questions:

1. What does a parent need to know about a child's subtree to finish its own answer? (Often: the best value if the child is used in some way, and the best value if not.)
2. Is that a handful of numbers, independent of the subtree size? If not, the DP is probably too slow (knapsack-style states grow with the subtree; see section 10).
3. Does the combination of two children commute? Then the order of the loop does not matter.
4. Is the answer needed per node, or only at the root? Only at the root: one pass. Per node and for every root: rerooting (section 8).

## 2. All the subtree DPs

Here is the code for sections 3 to 7. The walk-through of each function follows the listing.

```cpp
// Every subtree DP below walks `order` backwards: a node's children come after it in `order`,
// so by the time we reach v all of its children are done. No recursion.

struct IndepSet { long long weight; std::vector<int> nodes; };

// Maximum weight independent set (no two chosen nodes adjacent); weights may be negative, the empty set is allowed.
inline IndepSet max_weight_independent_set(const RootedTree& t, const std::vector<long long>& w) {
    if (w.size() != static_cast<std::size_t>(t.n)) throw std::invalid_argument("independent set: weights");
    std::vector<long long> skip(t.n, 0), take(t.n);
    for (int v = 0; v < t.n; ++v) take[v] = w[v];
    for (int i = t.n - 1; i >= 1; --i) {
        int v = t.order[i], p = t.parent[v];
        skip[p] += std::max(skip[v], take[v]);                 // p not chosen: the child is free
        take[p] += skip[v];                                    // p chosen: the child is not
    }
    std::vector<char> chosen(t.n, 0);                          // walk down and read the decisions
    chosen[t.root] = take[t.root] > skip[t.root];
    for (int i = 1; i < t.n; ++i) {
        int v = t.order[i];
        chosen[v] = !chosen[t.parent[v]] && take[v] > skip[v];
    }
    IndepSet r{std::max(skip[t.root], take[t.root]), {}};
    for (int v = 0; v < t.n; ++v) if (chosen[v]) r.nodes.push_back(v);
    return r;
}

// Number of independent sets (the empty set included), modulo mod (1 <= mod <= 2^31).
inline long long count_independent_sets(const RootedTree& t, long long mod) {
    if (mod < 1 || mod > (1LL << 31)) throw std::invalid_argument("count_independent_sets: mod");
    std::vector<long long> skip(t.n, 1 % mod), take(t.n, 1 % mod);
    for (int i = t.n - 1; i >= 1; --i) {
        int v = t.order[i], p = t.parent[v];
        skip[p] = skip[p] * ((skip[v] + take[v]) % mod) % mod;
        take[p] = take[p] * skip[v] % mod;
    }
    return (skip[t.root] + take[t.root]) % mod;
}

// Maximum matching of a tree, greedily from the leaves: match v with its parent when both are still free.
// A leaf-first choice is never worse (an exchange argument; the tests compare with brute force).
inline std::vector<std::pair<int, int>> max_matching(const RootedTree& t) {
    std::vector<char> used(t.n, 0);
    std::vector<std::pair<int, int>> m;
    for (int i = t.n - 1; i >= 1; --i) {
        int v = t.order[i], p = t.parent[v];
        if (!used[v] && !used[p]) { used[v] = used[p] = 1; m.push_back({v, p}); }
    }
    return m;
}

// Number of proper colourings with k <= 8 colours where node v may only use the colours in allowed[v] (bit c = colour c).
inline long long count_list_colorings(const RootedTree& t, int k, const std::vector<unsigned>& allowed, long long mod) {
    if (k < 1 || k > 8) throw std::invalid_argument("count_list_colorings: k");
    if (mod < 1 || mod > (1LL << 31)) throw std::invalid_argument("count_list_colorings: mod");
    if (allowed.size() != static_cast<std::size_t>(t.n)) throw std::invalid_argument("count_list_colorings: allowed");
    std::vector<long long> dp(static_cast<std::size_t>(t.n) * k), sum(t.n, 0);
    for (int v = 0; v < t.n; ++v) for (int c = 0; c < k; ++c) dp[static_cast<std::size_t>(v) * k + c] = (allowed[v] >> c & 1) ? 1 % mod : 0;
    for (int i = t.n - 1; i >= 0; --i) {
        int v = t.order[i];
        long long s = 0;
        for (int c = 0; c < k; ++c) s += dp[static_cast<std::size_t>(v) * k + c];
        sum[v] = s % mod;
        int p = t.parent[v];
        if (p < 0) break;
        for (int c = 0; c < k; ++c) {
            long long& d = dp[static_cast<std::size_t>(p) * k + c];
            d = d * ((sum[v] - dp[static_cast<std::size_t>(v) * k + c] % mod + mod) % mod) % mod;   // the child avoids colour c
        }
    }
    return sum[t.root];
}

// Largest sum of the node values on a path (at least one node; values may be negative).
inline long long max_path_sum(const RootedTree& t, const std::vector<long long>& val) {
    if (val.size() != static_cast<std::size_t>(t.n)) throw std::invalid_argument("max_path_sum: values");
    std::vector<long long> top1(t.n, 0), top2(t.n, 0);          // the two best non-negative downward chains below v
    long long best = LLONG_MIN;
    for (int i = t.n - 1; i >= 0; --i) {
        int v = t.order[i];
        best = std::max(best, val[v] + top1[v] + top2[v]);      // the best path whose highest node is v
        int p = t.parent[v];
        if (p < 0) continue;
        long long chain = val[v] + top1[v];                     // best chain starting at v going down
        if (chain > top1[p]) { top2[p] = top1[p]; top1[p] = chain; }
        else if (chain > top2[p]) top2[p] = chain;
    }
    return best;
}
```

## 3. Maximum weight independent set

An **independent set** is a set of nodes no two of which are joined by an edge. Choose one of maximum total weight; weights may be negative (such a node is simply never worth taking) and the empty set is allowed.

The state of a node `v` is two numbers, the best weight inside the subtree of `v`:

- `take[v]`: with `v` chosen. Then no child may be chosen: `take[v] = w[v] + sum of skip[c]`.
- `skip[v]`: with `v` not chosen. Every child is free: `skip[v] = sum of max(skip[c], take[c])`.

The code accumulates these in place: `take[p]` starts at `w[p]` and `skip[p]` at 0, and each child adds its contribution when the loop reaches it. The answer is `max(skip[root], take[root])`.

**Trace.** The 10-node tree of Topic 24 has edges `(0,1) (0,2) (1,3) (1,4) (2,5) (4,6) (4,7) (5,8) (8,9)`; rooted at 0 its `order` is `0 1 3 4 6 7 2 5 8 9`. Give the nodes the weights `w = 4 2 6 3 5 1 7 2 8 3` (node 0 has weight 4, node 1 weight 2, and so on). The loop finishes with

```text
node  :  0   1   2   3   4   5   6   7   8   9
w     :  4   2   6   3   5   1   7   2   8   3
skip  : 26  12   8   0   9   8   0   0   3   0
take  : 24  11  14   3   5   4   7   2   8   3
```

For example node 5 has the single child 8, with `skip[8] = 3` and `take[8] = 8`: `skip[5] = max(3, 8) = 8` and `take[5] = 1 + skip[8] = 4`. At the root `skip = 26` beats `take = 24`, so the maximum weight is **26**.

**Reconstruction** reads the decisions top-down, which is the forward direction of `order`. The root is chosen if `take[root] > skip[root]`. Any other node is chosen if its parent is **not** chosen and `take[v] > skip[v]`. Here: 0 is not chosen (24 < 26); 1 is not (11 < 12); 2 is (14 > 8); 3 is (3 > 0); 4 is not (5 < 9); 6 and 7 are (7 > 0, 2 > 0); 5 is not (its parent 2 is chosen); 8 is (8 > 3, parent 5 not chosen); 9 is not (parent 8 chosen). The set is **{2, 3, 6, 7, 8}** with weight 6 + 3 + 7 + 2 + 8 = 26, and no two of them are adjacent.

Two details of the reconstruction matter. The test `!chosen[parent]` is what keeps the set independent: without it, a node whose `take` is larger than its `skip` would be chosen even when its parent was, and the printed weight would no longer match the set. And the comparison is a **strict** `>`: on a tie the node is **not** taken, so zero-weight nodes never appear in the answer and the result is the same on every run. The tests check both on a two-node tree and on all-zero weights.

## 4. Counting independent sets

The same two states count instead of maximise: replace "sum of" by "product of" and `max` by `+`.

- `take[v] = prod of skip[c]`, `skip[v] = prod of (skip[c] + take[c])`, both starting at 1;
- the answer is `skip[root] + take[root]`, counting the empty set.

The numbers grow like Fibonacci numbers, so the function works modulo `mod` (1 to 2^31). Two products each below 2^31 stay below 2^62, which fits a signed 64-bit integer, and the sum `skip + take` is reduced before the multiplication. The starting value is `1 % mod` and not `1`, so that `mod = 1` gives 0 as it should.

On the 10-node tree the answer is **162**. Small checks by hand: a path of 5 nodes has 13 independent sets (the Fibonacci numbers 2, 3, 5, 8, 13 for paths of 1 to 5 nodes), and a star with k leaves has 2^k + 1 (any subset of the leaves, or the centre alone), so 65 for k = 6.

## 5. Maximum matching, minimum vertex cover

A **matching** is a set of edges no two of which share a node. On a tree a greedy rule is optimal:

```text
for v in reverse preorder:
    if v and parent[v] are both unmatched: match them
```

Why it works. When the loop reaches `v`, all of `v`'s descendants have already been handled, so if `v` is still free, everything below it is matched or cut off and `v` is a **leaf** of what remains. Take a maximum matching that does not use the edge (v, parent). If the parent is free, the edge can be added, which contradicts maximality; so the parent is matched to some other node x, and swapping (parent, x) for (parent, v) keeps the size. So matching a free leaf with its parent never loses. The tests compare the greedy size with brute force on every edge subset of every tree up to 12 nodes.

**The order matters.** Go through the nodes in the wrong order and the greedy fails. Take root 0 with children 1 and 3, and node 2 below 1 (edges `(0,1) (1,2) (0,3)`, preorder `0 1 2 3`). Reverse order matches `(3,0)` and then `(2,1)`: two edges. Forward order matches `(1,0)` first, after which 2 and 3 both have a used parent: one edge.

On the 10-node tree the code returns four edges, `(9,8) (5,2) (7,4) (3,1)`.

**Vertex cover.** A set of nodes touching every edge. Taking both ends of a matching edge is a cover, and no cover is smaller than a maximum matching (each matching edge needs its own node), and **König's theorem** says that on a bipartite graph (every tree is one) the two are equal: minimum vertex cover = maximum matching = 4 here. The complement of a vertex cover is an independent set, so with unit weights the largest independent set has **n - matching = 10 - 4 = 6** nodes. The tests check `matching == minimum cover` by brute force, and the benchmark checks `unit independent set == n - matching` on trees of a million nodes (section 9).

## 6. List colourings

Colour the nodes with `k` colours so that adjacent nodes differ; node `v` may only use the colours in a set `allowed[v]` (a bitmask). The state is a table `dp[v][c]` = the number of valid colourings of the subtree of `v` in which `v` has colour `c`:

`dp[v][c] = [c is allowed at v] * prod over children c' of ( sum over colours d != c of dp[c'][d] )`.

The inner sum is `sum[c'] - dp[c'][c]`, where `sum[c']` is the total over all colours: one subtraction and not a loop over `k - 1` colours, so the cost is O(n k). The code keeps `sum[v]` as it finalises each node, and takes the difference modulo `mod` with `+ mod` before the last `%`, because a plain subtraction can be negative.

Checks: with every colour allowed, any tree has exactly `k (k - 1)^(n - 1)` colourings (the root has k choices and each other node k - 1), which the tests confirm up to 1000 nodes and the benchmark confirms at 10^6 (3 colours: 852,563,092 modulo 10^9 + 7). With 4 colours the 10-node tree has 4 * 3^9 = **78,732**.

With restrictions: node 0 only colour 0, node 9 only colour 1, node 3 colours 1 or 2, three colours in all: **80**. Split at the root: with node 0 fixed, the subtree {1, 3, 4, 6, 7} contributes 16 colourings and {2, 5, 8, 9} contributes 5, and 16 * 5 = 80.

## 7. Maximum path sum

Find a path (any two nodes and everything between them, at least one node) with the largest sum of node values; values may be negative. The path has a **highest node** `v`, and it goes down from `v` into at most two different children. So per node keep the two best **downward chains** that start at a child, counting only chains of non-negative value (a negative chain is better left out, so they start at 0):

- `top1[v]`, `top2[v]`: the best and second best chain values among the children;
- the best path with highest node `v` has value `val[v] + top1[v] + top2[v]`;
- the chain that `v` offers its parent is `val[v] + top1[v]`.

The answer is the maximum over all `v`, and the running maximum starts at the smallest `long long`, not at 0, so that a tree whose values are all negative returns its largest single value (the tests include the case; for values -5, -2, -9 on a path the code returns -2).

**Trace.** Values `val = -3 4 -2 5 1 -6 2 3 -1 7` on the same tree.

```text
node   : 9   8   5   2   7   6   4   3   1   0
val    : 7  -1  -6  -2   3   2   1   5   4  -3
top1   : 0   7   6   0   0   0   3   0   5   9
top2   : 0   0   0   0   0   0   2   0   4   0
through: 7   6   0  -2   3   2   6   5  13   6
```

The nodes are listed in the order the loop visits them. At node 4 the children 6 and 7 offer chains 2 and 3, so `top1 = 3`, `top2 = 2` and the path through 4 is 1 + 3 + 2 = 6. Node 4 offers its parent the chain 1 + 3 = 4. At node 1 the children offer 5 (from 3) and 4 (from 4): through 1 is 4 + 5 + 4 = **13**, the answer, for the path 7, 4, 1, 3 (3 + 1 + 4 + 5). The chains from 5 and 2 are not worth taking: node 2 has value -2 and node 5's best chain is -6 + 6 = 0, which does not beat the starting 0.

## 8. Rerooting

**The problem.** Suppose the answer for a node `r` is "some function of the tree as seen from `r`": the sum of distances from `r` to all others, the distance to the farthest node, the size of the largest piece when `r` is removed. One subtree DP gives the answer for **one** root. For all n roots we would pay O(n^2).

**The idea.** Look at the answer for `r` as a combination of **messages**, one from each neighbour. The message from neighbour `u` describes the part of the tree behind `u` (the branch that contains `u` once the edge to `r` is cut). A message depends only on `u` and on the messages `u` receives from its **other** neighbours. In the rooted tree there are two kinds:

- `down[v]`: the message `v` sends to its **parent**. Its inputs are `v`'s children's `down` messages. This is exactly the subtree DP and is computed in the same backward loop.
- `up[c]`: the message the **parent** `v` sends to child `c`. Its inputs are `v`'s own `up[v]` (from above) and the `down` messages of all of `v`'s **other** children.

Once both are known, the answer for `v` is `finish` of the merge of `up[v]` and all the children's `down` messages.

**Four functions define a problem.**

```text
id()          the empty message
merge(a, b)   combine two messages (associative, commutative)
edge(agg, v)  the message v sends to a neighbour, from the merged messages agg of v's OTHER neighbours
finish(agg,v) the answer at v from the merged messages of ALL neighbours
```

**Why prefix and suffix merges.** To get `up[c]` for child number `j` of `v`, we need the merge of everything except child `j`. If `merge` had an inverse (sums do), we could subtract `down[c]` from the total. But `max` has no inverse: knowing the maximum of all branches does not tell the maximum without one of them. The fix is general: keep `pre[j]` = `up[v]` merged with the `down` of children `0 .. j-1`, and `suf[j]` = the merge of children `j .. k-1`. Then "all except child `j`" is `merge(pre[j], suf[j+1])`. A node with `k` children does O(k) merges, so the whole tree costs O(n) merges. The code does this with two small arrays reused for every node.

```cpp
// Rerooting: one answer per node, as if that node were the root, in O(n) merges.
// A problem P supplies a message type M and a result type R:
//   M id()                      the empty aggregate
//   M merge(M a, M b)           associative and commutative
//   M edge(M agg, int node)     the message `node` sends to a neighbour, given the merged messages of its OTHER neighbours
//   R finish(M agg, int node)   the answer for `node` from the merged messages of ALL its neighbours
// "Messages from the other neighbours" are what a subtree DP calls the children, and the one from the parent is `up`.
template <class P>
std::vector<typename P::R> reroot(const RootedTree& t, const P& p) {
    using M = typename P::M;
    std::vector<M> down(t.n, p.id()), up(t.n, p.id());           // down[v]: message v sends to its parent; up[c]: parent sends to c
    for (int i = t.n - 1; i >= 0; --i) {
        int v = t.order[i];
        M agg = p.id();
        for (int e = t.cstart[v]; e < t.cstart[v + 1]; ++e) agg = p.merge(agg, down[t.clist[e]]);
        down[v] = p.edge(agg, v);
    }
    std::vector<typename P::R> ans(t.n);
    std::vector<M> pre, suf;
    for (int i = 0; i < t.n; ++i) {
        int v = t.order[i], b = t.cstart[v], k = t.cstart[v + 1] - b;
        pre.assign(k + 1, p.id()); suf.assign(k + 1, p.id());
        pre[0] = up[v];
        for (int j = 0; j < k; ++j) pre[j + 1] = p.merge(pre[j], down[t.clist[b + j]]);
        for (int j = k - 1; j >= 0; --j) suf[j] = p.merge(suf[j + 1], down[t.clist[b + j]]);
        ans[v] = p.finish(pre[k], v);
        for (int j = 0; j < k; ++j)                               // everything except child j, plus what comes from above
            up[t.clist[b + j]] = p.edge(p.merge(pre[j], suf[j + 1]), v);
    }
    return ans;
}

// Sum of the distances from each node to all the others.
struct SumDistances {
    using M = std::pair<long long, long long>;                    // (nodes in the branch, sum of their distances to the sender's neighbour)
    using R = long long;
    M id() const { return {0, 0}; }
    M merge(const M& a, const M& b) const { return {a.first + b.first, a.second + b.second}; }
    M edge(const M& a, int) const { return {a.first + 1, a.second + a.first + 1}; }
    R finish(const M& a, int) const { return a.second; }
};
// Eccentricity: the distance from each node to the farthest node.
struct Eccentricity {
    using M = int; using R = int;
    M id() const { return 0; }
    M merge(M a, M b) const { return std::max(a, b); }
    M edge(M a, int) const { return a + 1; }
    R finish(M a, int) const { return a; }
};
// The size of the largest component left when the node is removed.
struct LargestPart {
    using M = std::pair<int, int>;                                // (total size of the branches, size of the largest branch)
    using R = int;
    M id() const { return {0, 0}; }
    M merge(const M& a, const M& b) const { return {a.first + b.first, std::max(a.second, b.second)}; }
    M edge(const M& a, int) const { return {a.first + 1, a.first + 1}; }
    R finish(const M& a, int) const { return a.second; }
};
```

The first loop is the backward pass (the `down` messages, the subtree DP). The second goes forward in `order`: for each `v`, `up[v]` is final because its parent was handled earlier. It builds `pre` and `suf` over `v`'s children, computes the answer, and hands `up` to every child.

### Problem 1: sum of distances

The message is a pair **(count, sum)**: how many nodes lie in the branch, and the sum of their distances **to the receiver**. `edge` takes the merged messages of the sender's other neighbours, adds the sender itself, and moves every node one edge further from the receiver: `(c, s)` becomes `(c + 1, s + c + 1)`. `finish` returns the sum.

On the 10-node tree (the table shows each node's `down` message to its parent, the `up` message from its parent, and the answer):

```text
node   :       0        1        2        3        4        5        6        7        8        9
down   : (10,31)   (5,11)   (4,10)    (1,1)    (3,5)    (3,6)    (1,1)    (1,1)    (2,3)    (1,1)
up     :   (0,0)   (5,15)   (6,17)   (9,29)   (7,23)   (7,24)   (9,33)   (9,33)   (8,32)   (9,41)
answer :      21       21       23       29       25       27       33       33       33       41
```

Read node 4: it has children 6 and 7, each sending (1, 1), and its parent sends `up[4] = (7, 23)`: seven nodes (0, 1, 2, 3, 5, 8, 9) at total distance 23 from node 4. The merge is (9, 25), and the answer is **25**. Check one directly: distances from 4 to 0, 1, 2, 3, 5, 6, 7, 8, 9 are 2, 1, 3, 2, 4, 1, 1, 5, 6, which sum to 25.

**When there is an inverse, an O(1) shortcut exists.** For this particular problem moving the root from a parent `p` to a child `c` brings the `size[c]` nodes of `c`'s subtree one step closer and sends the other `n - size[c]` one step further: `ans[c] = ans[p] + n - 2 * size[c]`. From `ans[0] = 21`: `ans[1] = 21 + 10 - 10 = 21` (size 5) and `ans[3] = 21 + 10 - 2 = 29` (size 1) as in the table. This needs only `size` and one forward pass, and is the standard solution of the LeetCode problem; rerooting with messages is what you reach for when the quantity has no such inverse (the next two problems).

### Problem 2: eccentricity

The **eccentricity** of a node is the distance to the farthest node. The message is a single number, the length of the longest path into the branch from the receiver: `merge = max`, `edge(a) = a + 1`, `finish = a`. The identity is 0 (a leaf has nothing beyond it, so it sends 0 + 1 = 1).

```text
node   : 0  1  2  3  4  5  6  7  8  9
down   : 5  3  4  1  2  3  1  1  2  1
up     : 0  5  4  6  6  5  7  7  6  7
answer : 4  5  4  6  6  5  7  7  6  7
```

Node 4 has `up[4] = 6` (the farthest node behind the parent side is 9, six edges away) and its children offer 1, so the answer is 6. The answers tie the earlier topics together: the **largest** eccentricity (7) is the diameter and the **smallest** (4, at nodes 0 and 2) are exactly the **centers** of Topic 24. The tests and the benchmark both check these identities.

### Problem 3: the largest part when a node is removed

Remove node `v`; the tree falls into one component per neighbour. The message is the pair **(total, largest)**: the sum of the branch sizes and the largest branch size. `edge(a) = (a.total + 1, a.total + 1)` (a branch seen from outside is one component of size `total + 1`); `merge` adds the totals and takes the larger of the largest; `finish` returns `largest`.

```text
node   :  0  1  2  3  4  5  6  7  8  9
down   : 10  5  4  1  3  3  1  1  2  1     (the size of each subtree)
up     :  0  5  6  9  7  7  9  9  8  9     (the size of the rest of the tree)
answer :  5  5  6  9  7  7  9  9  8  9
```

A node is a **centroid** when its largest part is at most n / 2, i.e. `2 * answer <= n`: here nodes 0 and 1 (answer 5 each, with n = 10), the centroids of Topic 24. This problem's `down` and `up` are just subtree sizes and complements, but the pair form (total, largest) is what lets the machine run unchanged.

## 9. What it costs

Numbers below are **run 1 / run 2**, milliseconds, 10^6 nodes, best of 3.

| Tree | Independent set | Count | Matching | List colouring (k = 3) | Path sum |
|---|---|---|---|---|---|
| Random | 37.5 / 39.3 | 25.6 / 27.0 | 11.6 / 12.3 | 65.6 / 66.2 | 29.3 / 28.2 |
| Path | 33.1 / 33.9 | 27.9 / 27.4 | 6.0 / 5.9 | 68.4 / 72.2 | 17.9 / 19.0 |
| Star | 28.5 / 27.1 | 21.8 / 21.8 | 4.4 / 4.5 | 58.6 / 57.5 | 18.1 / 16.9 |

| Tree | Sum of distances | Eccentricity | Largest part |
|---|---|---|---|
| Random | 87.9 / 85.0 | 79.1 / 73.8 | 69.7 / 69.0 |
| Path | 77.2 / 68.5 | 72.4 / 60.5 | 79.6 / 62.9 |
| Star | 74.6 / 66.5 | 45.6 / 52.5 | 51.0 / 41.4 |

What holds in both runs: every DP finishes in at most 72.2 ms for a million nodes; **matching is the cheapest** of the five on every tree (12.3 ms at most) and **colouring is the dearest** (57.5 to 72.2 ms; each node carries three table entries and not two values); and each rerooting problem takes between 41.4 and 87.9 ms, all n answers together. I draw no conclusion about which tree shape is fastest, or about which of the three rerooting problems is cheapest: on the path and the star their order changes between the two runs.

The rerooting answers were also checked at this size, on all three trees, by identities that do not use the rerooting code:

- the sum over all nodes of the sum-of-distances answers equals 2 * sum over edges of `size * (n - size)` (random tree: 24,200,819,616,802; path: 333,333,333,333,000,000; star: 1,999,996,000,002);
- the largest eccentricity equals the diameter (59 on the random tree, 999,999 on the path, 2 on the star), the nodes of smallest eccentricity equal the centers, and the nodes with `2 * largest <= n` equal the centroids;
- with unit weights the independent set has `n - matching` nodes (595,991 on the random tree, 500,000 on the path, 999,999 on the star), the reconstructed set is independent and its weight matches the reported maximum.

**Against the naive method.** On a random tree of 3000 nodes, rerooting took **0.137 / 0.080 ms** for all sums of distances, and a breadth-first search from every node (the n * O(n) method) took **34.8 / 34.5 ms**, the same answers: at least 250 times faster. The naive method grows like n^2, so at a million nodes it would be 10^12 steps (an extrapolation, not a measurement); rerooting grows like n.

## 10. Choosing

| Question | Use |
|---|---|
| One answer for the whole tree from node choices (independent set, cover, colouring, path sum) | A single subtree DP, section 2 |
| Same, count of solutions | The same states with `+` and `*` modulo `mod` |
| An answer for every node as the root | Rerooting, section 8 |
| Sum-like answer for every root, and an inverse exists | The O(1) shortcut `ans[c] = ans[p] + ...` |
| Answers for many arbitrary paths | LCA and path queries (Topic 25), heavy-light decomposition (Topic 27) |
| Distance counting over all pairs | Centroid decomposition (Topic 27) |
| Unrooted problem needing only the "middle" | Centers and centroids (Topic 24) |

A DP whose state grows with the subtree (for example "the best value using exactly j nodes of this subtree", a tree knapsack) is the next level: merging two tables costs the product of their sizes and a size argument bounds the total at O(n^2). That is from general knowledge and is **not** implemented or tested here.

## 11. Common mistakes

- **Recursive DFS on a path.** 10^6 frames overflow the stack. Loop over `order` backwards.
- **Reconstruction without the parent test.** `chosen[v] = take[v] > skip[v]` alone can choose a node and its parent. The weight printed is right but the set is not independent.
- **`>=` instead of `>`.** Still a correct weight, but ties take zero-weight nodes and the reported set can change.
- **Matching in forward order.** Section 5 has a four-node counterexample.
- **Plain subtraction in modular arithmetic.** `sum - dp` can be negative; add `mod` before the final `%`.
- **Starting the counts at 1 with `mod = 1`.** The answer must be 0; use `1 % mod`.
- **Starting the best path sum at 0.** An all-negative tree must return its largest single value. Start at the smallest `long long`.
- **Taking a negative chain.** A chain of negative value must not be extended, so `top1` and `top2` start at 0.
- **Rerooting: leaving out `up[v]` in the answer,** or using the merged value of **all** neighbours where `edge` needs the **other** ones. The prefix and suffix arrays exist for this.
- **A merge that is not associative and commutative** (an average, say) gives answers that depend on the child order.
- **Forgetting that `RootedTree` must be built first.** The functions assume `order` lists parents before children; Topic 24's class guarantees it.

The five equivalent mutants (changes that do not alter any output, so no test can catch them): `1` instead of `1 % mod` as the starting count in two places (the final reduction `% mod` hides the difference, and with `mod = 1` both give 0); `break` instead of `continue` when the colouring loop reaches the root, which is the last node it visits; stopping the `down` loop before the root, whose `down` message is never used; and stopping the suffix loop before index 0, since `suf[0]` is never read.

## 12. Questions to answer out loud

1. **How do you do a tree DP without recursion?** Loop over the DFS preorder backwards; every child is processed before its parent.
2. **State for maximum independent set?** Two numbers per node: best with the node taken, best with it not taken. Taken adds the children's "not taken", not-taken adds each child's better one.
3. **How do you get the actual set and not only the weight?** A forward pass: take a node if its parent was not taken and its `take` beats its `skip`.
4. **Why is greedy matching correct on a tree?** A leaf matched to its parent never loses (swap argument); processing in reverse preorder makes each free node a leaf.
5. **What is rerooting and why does it need prefix and suffix arrays?** Answers for all roots in O(n); the message to one child needs the merge of all the others, and not every merge has an inverse.
6. **When can you skip rerooting?** When the quantity has an inverse, as for the sum of distances: `ans[c] = ans[p] + n - 2 size[c]`.
7. **How do you count 3-colourings of a tree?** 3 * 2^(n - 1); with lists, a DP per colour with the child sum minus the colour of the parent.

## 13. Problems

**A.** With the weights `w = 4 2 6 3 5 1 7 2 8 3` find `skip` and `take` for nodes 9, 8 and 5, then for node 1.

**B.** Count the independent sets of a path of 5 nodes, of a star with 6 leaves, and of the 10-node tree.

**C.** Show the order in which the greedy matching must process nodes by finding the four-node tree where the forward order is worse. What is the minimum vertex cover of the 10-node tree, and the largest independent set with unit weights?

**D.** Count the proper 4-colourings of the 10-node tree. Then count the 3-colourings in which node 0 has colour 0, node 9 colour 1 and node 3 colour 1 or 2, by multiplying the counts of the two subtrees under the root.

**E.** For `val = -3 4 -2 5 1 -6 2 3 -1 7`, find the best path and its sum. What does the code return for values -5, -2, -9 on a path?

**F.** Using the table of section 8, check the sum-of-distances answer of node 5 from `up[5]` and its child's `down` message.

**G.** From the eccentricity and largest-part tables, which nodes are the centers and which the centroids of the 10-node tree?

**H.** Design the messages for "for every node v, the maximum weight independent set that contains v". (Idea only; not part of the tested code.)

### Answers

**A.** Node 9 (leaf): `skip = 0`, `take = 3`. Node 8 (child 9): `skip = max(0, 3) = 3`, `take = 8 + 0 = 8`. Node 5 (child 8): `skip = max(3, 8) = 8`, `take = 1 + 3 = 4`. Node 1 (children 3 and 4): `skip[3] = 0, take[3] = 3`; `skip[4] = 9, take[4] = 5`; so `skip[1] = 3 + 9 = 12` and `take[1] = 2 + 0 + 9 = 11`.

**B.** 13, 65 and 162. The path: 2, 3, 5, 8, 13 for 1 to 5 nodes. The star: 2^6 + 1 = 65.

**C.** Edges `(0,1) (1,2) (0,3)`, preorder `0 1 2 3`. Reverse order gives `(3,0), (2,1)`: two edges. Forward order gives `(1,0)`: one edge. The 10-node tree has a matching of 4, so the minimum vertex cover is 4 and the largest unit independent set is 6.

**D.** 4 * 3^9 = 78,732. With the list restrictions, the subtree {1, 3, 4, 6, 7} gives 16 and the subtree {2, 5, 8, 9} gives 5; 16 * 5 = 80.

**E.** The best path is 7, 4, 1, 3 with sum 3 + 1 + 4 + 5 = 13. For -5, -2, -9 the code returns -2.

**F.** `up[5] = (7, 24)` and the child 8 sends `(2, 3)`, so the merged message is (9, 27) and the answer is 27. Directly: the distances from 5 to 2, 0, 1, 3, 4, 6, 7, 8, 9 are 1, 2, 3, 4, 4, 5, 5, 1, 2, which sum to 27.

**G.** Centers: nodes 0 and 2 (eccentricity 4, the minimum). Centroids: nodes 0 and 1 (largest part 5 = n / 2).

**H.** Rerooting with the message `(skip, take)` of a branch: `edge` turns the merged messages of the other neighbours into the branch's own pair (`take = w + sum of skip`, `skip = sum of max(skip, take)`), `merge` adds componentwise, and `finish` for node v is `w[v] + sum of skip` over all neighbours' branches, which is the best set that contains v.

---

## Cheat sheet

- **Subtree DP:** loop `order` backwards; state per node; fold child into parent. No recursion.
- **Independent set:** `take = w + sum skip(c)`, `skip = sum max(skip(c), take(c))`; reconstruct top-down with the parent test; ties do not take.
- **Counting:** the same with `*` and `+` modulo `mod`; start at `1 % mod`.
- **Matching:** reverse preorder, match a free node with a free parent. Cover = matching (König). Unit independent set = n - matching.
- **Colourings:** `dp[v][c]` with the child factor `sum[c'] - dp[c'][c]`; any tree has `k (k - 1)^(n - 1)`.
- **Path sum:** keep two best non-negative downward chains; best = `val + top1 + top2`; start the maximum at the smallest value.
- **Rerooting:** `down` backward, `up` forward; all-except-one by prefix and suffix merges; four functions `id`, `merge`, `edge`, `finish`.
- **Identities:** max eccentricity = diameter; min eccentricity nodes = centers; `2 * largest part <= n` = centroids; sum of distances: `ans[c] = ans[p] + n - 2 size[c]`.
- **Measured (10^6 nodes):** each DP 4.4 to 72.2 ms; each rerooting problem 41.4 to 87.9 ms; at 3000 nodes at least 250 times faster than a search from every node.

---

## Sources

- The subtree DPs and rerooting are standard: I name no single article for them: both are taught in many competitive-programming tutorials (from memory); König's theorem and the matching greedy for trees are in any graph theory text (CLRS treats matching in its bipartite chapter; from memory, not checked against the book). The code is my own implementation, tested as above.
- The claims about the default 8 MiB Linux stack and about tree knapsack bounds are from general knowledge and were not measured or implemented here. All timings are measured here.
- LeetCode, by name: House Robber III (independent set on a tree), Binary Tree Maximum Path Sum, Sum of Distances in Tree (the O(1) shortcut form), Minimum Height Trees (centers), Binary Tree Cameras and Maximum Number of K-Divisible Components (other subtree DPs).
