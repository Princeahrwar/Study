# DSA 14.01: P, NP and What to Do With a Hard Problem

*Part of the [Theory and advanced techniques plan](00_plan.md) · Time: about 7 hours (3.5 reading, 3.5 problems) · You need: bitmask DP from [Dynamic programming](../10_dynamic_programming/00_plan.md), graphs from [Graphs](../08_graphs/00_plan.md), and backtracking from [Recursion and backtracking](../06_recursion_and_backtracking/00_plan.md)*

Every other tutorial in this course gave you a problem with a polynomial algorithm. This one is about the problems that do not seem to have one. In an interview the skill is rarely to *prove* hardness. It is to **notice** that the problem you were handed is a known hard one, say so in one sentence, and then choose a sensible response: an exact search that is fast enough for the stated size, a dynamic program over subsets, an approximation with a guarantee, a heuristic, or a special structure that makes the instance small. Each of those responses is built and measured below.

By the end you can:

- define P and NP through **certificates**: checking an answer is cheap, finding one may not be (measured on Hamiltonian cycle);
- name the tractable twin of a hard problem (shortest and longest path, Euler and Hamilton, 2-SAT and 3-SAT, and so on) and say what changed;
- use a **reduction** in the right direction, and write two of them as code: independent set, vertex cover and clique are one problem, and 3-SAT becomes independent set;
- solve a hard problem exactly when it is small: all subsets, branching with pruning, and Held-Karp for the travelling salesman, each compared with the naive search (measured);
- give a 2-approximation for metric TSP and measure how good it and two heuristics are in practice;
- use a small parameter (vertex cover with parameter `k`) or small numbers (subset sum) to turn a hard problem into a fast one, and say why that does not contradict hardness;
- state in an interview what you can and cannot promise for an NP-hard problem.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 1,144,519 checks passed under AddressSanitizer and UBSan. Each solver is compared with a deliberately plain oracle (all subsets, all permutations, all assignments), and the identities used (the independent-set, cover and clique relations, the reduction from 3-SAT, the factor 2 of the MST tour, the call bound of the cover search) are tested directly, not assumed. I tried 19 deliberate bugs and all were caught; one of them (a 2-opt that reverses the wrong stretch) was caught only because the tests stopped terminating, so it counts as caught by timeout rather than by a wrong answer. A twentieth edit I tried changed no behaviour, so no test could notice it. I removed it instead of counting it.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Graphs and cities are random and seeded, so treat the numbers as the shape of the effect and not as a prediction for your data. Statements about theory (what is known to be NP-complete, the best known exponents) are from memory and are marked.

---

## 1. P, NP, and certificates

**P** is the set of decision problems (yes or no answers) solvable in time polynomial in the input size. **NP** is the set whose "yes" answers have a **certificate** that can be checked in polynomial time. A problem is **NP-hard** if every problem in NP reduces to it, and **NP-complete** if it is both in NP and NP-hard. Whether P equals NP is open. Nobody has a polynomial algorithm for any NP-complete problem, and nobody has proved there is none. In practice you treat NP-complete as "no efficient exact algorithm is known, and finding one would be famous".

The cleanest way to feel the difference is to take one problem and do both halves. A **Hamiltonian cycle** visits every vertex of a graph exactly once and returns to the start.

- **Checking** a proposed cycle: is it a permutation of the vertices, and is each consecutive pair (including last to first) an edge? That is `O(n)`.
- **Finding** one: the fastest method I know for the general case is the subset DP below, which takes `O(2^n * n)`. (Better exponential bounds exist; from memory, nothing polynomial is known.)

```cpp
struct Graph {                                 // at most 64 vertices; adj[v] is a bit mask of the neighbours of v
    int n = 0;
    vector<uint64_t> adj;
    explicit Graph(int nn = 0) : n(nn), adj(nn, 0) {}
    void add_edge(int u, int v) { if (u != v) { adj[u] |= 1ULL << v; adj[v] |= 1ULL << u; } }
    bool has_edge(int u, int v) const { return adj[u] >> v & 1; }
};

// Verifying a certificate is fast. For a Hamiltonian cycle: is `order` a permutation, and is each step an edge?
inline bool verify_hamiltonian_cycle(const Graph& g, const vector<int>& order) {
    int n = g.n;
    if (n < 3 || (int)order.size() != n) return false;
    uint64_t seen = 0;
    for (int v : order) {
        if (v < 0 || v >= n || (seen >> v & 1)) return false;
        seen |= 1ULL << v;
    }
    for (int i = 0; i < n; ++i) if (!g.has_edge(order[i], order[(i + 1) % n])) return false;
    return true;
}

// Finding one is the hard part. The best known exact methods are exponential; this is the subset DP:
// reach[S] = the set of vertices v such that some path starts at 0, visits exactly S and ends at v.
inline bool hamiltonian_cycle_dp(const Graph& g) {         // n <= 22: 2^n * 4 bytes, O(2^n * n) steps
    int n = g.n;
    if (n < 3) return false;
    vector<uint32_t> reach(1u << n, 0);
    reach[1] = 1;
    for (uint32_t S = 1; S < (1u << n); S += 2) {          // only sets that contain vertex 0
        for (uint32_t ends = reach[S]; ends; ends &= ends - 1) {
            int v = __builtin_ctz(ends);
            for (uint32_t nxt = (uint32_t)g.adj[v] & ~S; nxt; nxt &= nxt - 1) reach[S | (nxt & -nxt)] |= nxt & -nxt;
        }
    }
    return (reach[(1u << n) - 1] & (uint32_t)g.adj[0]) != 0;
}

// Subset sum: the certificate is a list of indices.
inline bool verify_subset_sum(const vector<long long>& a, long long target, const vector<int>& idx) {
    long long s = 0;
    vector<char> used(a.size(), 0);
    for (int i : idx) {
        if (i < 0 || i >= (int)a.size() || used[i]) return false;
        used[i] = 1; s += a[i];
    }
    return s == target;
}
```

The DP keeps, for every subset `S` of vertices that contains vertex 0, the set of vertices at which a path that starts at 0 and visits exactly `S` can end. Extending a path by one vertex is an `OR` of bit masks. The answer is yes if the full set can end at a neighbour of 0.

On a random graph of 20 vertices with a hidden cycle (plus a few random extra edges):

```
T1 random graph with 20 vertices and a hidden Hamiltonian cycle: the subset DP says yes; checking the hidden cycle says valid; checking the same cycle with two vertices swapped says invalid
```

Checking costs tens of nanoseconds per call; the DP costs a large fraction of a millisecond even on this sparse graph. The times:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| subset DP, 20 vertices, sparse (most subsets unreachable) | 0.61 / 0.68 |
| checking the hidden cycle, 100,000 times (so about 30 ns each) | 2.87 / 3.00 |

Sparse graphs flatter the DP, because most subsets are unreachable and skipped. On dense graphs every subset is reachable and the DP does the full work:

```
T1 dense random graphs (edge probability 0.9), subset DP says: 18 vertices yes, 20 vertices yes, 22 vertices yes
```

| Dense random graph (edge probability 0.9) | DP time (ms), run 1 / run 2 |
|---|---|
| 18 vertices | 11.7 / 12.0 |
| 20 vertices | 53.9 / 55.0 |
| 22 vertices | 256.3 / 272.1 |

Every two extra vertices multiply the time by 4.6 to 4.9, which is the `2^n` showing (a factor of about 2.2 per vertex). At 22 vertices the table is 16 MB; at 30 it would be 4 GB and, extrapolating the same factor, the time about a minute or more. That is the practical meaning of "exponential": the wall arrives at a size of 25 to 35, not at 1000.

**Common wrong picture.** "NP means not polynomial." It means *nondeterministic polynomial*: a yes answer can be verified in polynomial time. Every problem in P is also in NP. "NP-hard" does not mean "in NP" (the halting problem is NP-hard and not in NP).

## 2. The tractable twin

Many hard problems have a nearly identical-looking problem that is in P. Seeing the pair is the fastest way to remember which one you have. This table is standard (from memory); the second column is the one an interviewer hopes you will not be fooled by.

| Tractable (polynomial) | Hard (NP-hard or NP-complete) | What changed |
|---|---|---|
| Shortest path (Dijkstra, Bellman-Ford) | Longest **simple** path | Optimal substructure fails: the best path to `v` may block the best continuation |
| Euler circuit (every **edge** once) | Hamiltonian cycle (every **vertex** once) | A degree condition decides the first; nothing like it is known for the second |
| 2-SAT (clauses of two literals) | 3-SAT (clauses of three) | Two-literal clauses are implications, so strongly connected components solve it |
| 2-colouring (bipartite test) | 3-colouring | Two colours force every neighbour; three leave a real choice |
| Minimum spanning tree | Travelling salesman (visit all, return) | A tree has no return constraint |
| Bipartite matching | 3-dimensional matching | Flow solves the first; a third side breaks the structure |
| Minimum cut | Maximum cut | Flow solves the first |
| Fractional knapsack (greedy) | 0/1 knapsack | Pseudo-polynomial DP only (section 7) |
| Linear programming | Integer programming | Rounding a fractional solution does not stay feasible |

When a statement reads like a problem from the left column but demands "exactly once", "simple", "an integer", or "choose a subset", slow down and ask which column you are in.

## 3. Reductions

A **reduction from A to B** is a polynomial-time procedure that turns any instance of A into an instance of B with the same yes or no answer. It is a way of saying "B is at least as hard as A": a fast algorithm for B would give a fast algorithm for A.

> **The direction rule.** To show that **your** problem X is hard, reduce a **known hard** problem *to* X. If you reduce X to a known hard problem, you have shown nothing (you have shown X is no harder than it).

Getting this backwards is the most common error in an interview on this topic. Check it by asking: "which problem's instances am I converting, and into what?" The known-hard problem is the one being converted.

### 3.1 Independent set, vertex cover and clique are one problem

Three definitions, in a graph `G` with `n` vertices:

- an **independent set** is a set of vertices with no edge between any two;
- a **vertex cover** is a set of vertices touching every edge;
- a **clique** is a set of vertices with every pair joined.

The links:

```
S is independent in G   <=>   V \ S is a vertex cover of G         so  alpha + tau = n
S is a clique in G      <=>   S is independent in the complement   so  omega(G) = alpha(complement of G)
```

where `alpha` is the size of the largest independent set, `tau` the smallest vertex cover, `omega` the largest clique. The proof of the first line takes one sentence: an edge has both ends outside `S` exactly when `S` fails to be independent, hence when `V \ S` fails to cover. So solving any one of the three problems solves all three, in polynomial time. This is why people say "maximum independent set" in an interview and mean the whole family.

### 3.2 3-SAT to independent set

3-SAT: given a formula that is an AND of clauses, each an OR of three literals (`x` or `not x`), is there an assignment that makes it true? It was among the first problems proved NP-complete (Cook and Levin, from memory). The reduction to independent set is short enough to write in full:

1. For each clause, add a **triangle** of three vertices, one per literal. An independent set can take at most one vertex of a triangle, so at most one literal per clause.
2. Add an edge between every vertex for `x` and every vertex for `not x` in *different* clauses, so an independent set cannot pick contradicting literals.
3. The formula with `m` clauses is satisfiable if and only if the graph has an independent set of size `m`.

If satisfiable: pick one true literal in each clause, giving `m` vertices, with no two contradicting. If an independent set of size `m` exists: it takes exactly one vertex from each triangle, the chosen literals never contradict, and setting them true (anything else arbitrary) satisfies every clause. The code does both directions, including reading an assignment back off the set.

```cpp
// Independent set, vertex cover and clique are one problem seen three ways.
//   S is independent in G  <=>  V \ S is a vertex cover of G        (so alpha + tau = n)
//   S is a clique in G     <=>  S is independent in the complement   (so omega(G) = alpha(complement))
inline Graph complement(const Graph& g) {
    Graph h(g.n);
    for (int u = 0; u < g.n; ++u) for (int v = u + 1; v < g.n; ++v) if (!g.has_edge(u, v)) h.add_edge(u, v);
    return h;
}

// 3-SAT -> independent set. A literal is +v or -v for a variable v in 1..vars.
// One vertex per literal occurrence; a triangle per clause (pick at most one literal from each clause);
// an edge between x and not-x in different clauses (do not pick contradicting literals).
// The formula is satisfiable exactly when the graph has an independent set of size (number of clauses).
struct Cnf3 { int vars; vector<std::array<int, 3>> clauses; };

inline Graph sat3_to_independent_set(const Cnf3& f) {      // at most 21 clauses (63 vertices)
    int m = (int)f.clauses.size();
    Graph g(3 * m);
    for (int c = 0; c < m; ++c) for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) g.add_edge(3 * c + i, 3 * c + j);
    for (int c1 = 0; c1 < m; ++c1) for (int c2 = c1 + 1; c2 < m; ++c2)
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            if (f.clauses[c1][i] == -f.clauses[c2][j]) g.add_edge(3 * c1 + i, 3 * c2 + j);
    return g;
}

// The reduction also maps solutions back: read an assignment off the chosen vertices.
inline vector<bool> assignment_from_independent_set(const Cnf3& f, uint64_t chosen) {
    vector<bool> val(f.vars + 1, false);
    for (uint64_t m = chosen; m; m &= m - 1) {
        int v = __builtin_ctzll(m);
        int lit = f.clauses[v / 3][v % 3];
        val[std::abs(lit)] = lit > 0;
    }
    return val;
}
```

Two small cases, solved by the exhaustive search of the next section so that the answer does not depend on the reduction being right:

```
T2 graph with edges 01 02 12 23 34 (5 vertices): independent set 2, vertex cover 3, clique 3; independent set of the complement 3
```

The first line is the triangle with a tail: independent set 2 means cover 3, and clique 3 equals the independent set of the complement.

```
T3 four clauses over 3 variables: graph with 12 vertices and 24 edges, largest independent set 4 (= number of clauses, so satisfiable); the assignment read off it: x1=F x2=F x3=T and it satisfies every clause: yes
T3 all eight sign patterns over 3 variables (no assignment satisfies them all): graph with 24 vertices, largest independent set 7, which is less than the 8 clauses
```

The first formula is satisfiable, the independent set has size 4 (the number of clauses), and the assignment read off it satisfies every clause. The second formula contains all eight sign patterns of three variables, so every assignment falsifies one clause; the best independent set has size 7, one short.

**What this proves and does not.** The reduction shows independent set is at least as hard as 3-SAT, hence NP-hard. It does *not* show a particular graph is hard to solve. Most graphs are not.

## 4. Exact search for small instances

If the problem is NP-hard and the input is small (a few dozen items), the right answer is usually an **exact search made as clever as the size demands**. Hardness is about the worst case as `n` grows, not about 25 items.

### 4.1 All subsets

For maximum independent set the baseline is trying every subset of vertices, `O(2^n * n)`. It is correct and takes seconds up to `n` around 26, and it is the oracle for the rest of this tutorial.

### 4.2 Branching with reductions

Pick a vertex `v`. Either `v` is in the set (so its neighbours are out) or it is not. That is two subproblems, each on a smaller graph. Two things make the tree small:

- **Safe reductions.** A vertex with no remaining neighbour, or exactly one, can always be taken: by an exchange argument, some optimal set contains it (if it has one neighbour `u` and the optimum took `u` instead, swap `u` for `v`). That removes the vertex *without branching*.
- **Branch where it hurts most.** Choose the vertex of highest degree. Taking it removes the most vertices, so the "take" side shrinks fast.

```cpp
// Exhaustive: try every subset. 2^n * n steps.
inline std::pair<int, uint64_t> max_independent_set_brute(const Graph& g) {   // n <= 26
    int best = 0; uint64_t bm = 0;
    for (uint64_t S = 0; S < (1ULL << g.n); ++S) {
        bool ok = true;
        for (uint64_t m = S; m && ok; m &= m - 1) if (g.adj[__builtin_ctzll(m)] & S) ok = false;
        if (ok && __builtin_popcountll(S) > best) { best = __builtin_popcountll(S); bm = S; }
    }
    return {best, bm};
}

// Branching: pick a vertex v; either v is in the set (so its neighbours are out) or v is out.
// Two safe shortcuts shrink the tree: a vertex with no neighbour, or exactly one, can always be taken.
// Branch on a vertex of highest degree: taking it removes the most vertices.
struct MaxIndependentSet {
    const Graph& g;
    long long nodes = 0;                                   // calls made, to compare with 2^n
    explicit MaxIndependentSet(const Graph& gr) : g(gr) {}
    int solve(uint64_t alive) {
        ++nodes;
        if (!alive) return 0;
        int bv = -1, bd = -1;
        for (uint64_t m = alive; m; m &= m - 1) {
            int v = __builtin_ctzll(m);
            int d = __builtin_popcountll(g.adj[v] & alive);
            if (d <= 1) return 1 + solve(alive & ~(g.adj[v] | (1ULL << v)));
            if (d > bd) { bd = d; bv = v; }
        }
        int take = 1 + solve(alive & ~(g.adj[bv] | (1ULL << bv)));
        int leave = solve(alive & ~(1ULL << bv));
        return std::max(take, leave);
    }
    int run() { return solve(g.n == 64 ? ~0ULL : (1ULL << g.n) - 1); }
};
```

Calls made against the `2^n` subsets, on random graphs with edge probability `p`:

```
T4 independent set by branching; random graphs, edge probability p; calls made against 2^n subsets
T4 p=0.1 n=20: size 12, 13 calls, 2^n = 1.05e+06
T4 p=0.1 n=30: size 16, 20 calls, 2^n = 1.07e+09
T4 p=0.1 n=40: size 19, 59 calls, 2^n = 1.10e+12
T4 p=0.1 n=50: size 22, 291 calls, 2^n = 1.13e+15
T4 p=0.1 n=60: size 22, 5120 calls, 2^n = 1.15e+18
T4 p=0.3 n=20: size 7, 59 calls, 2^n = 1.05e+06
T4 p=0.3 n=30: size 9, 288 calls, 2^n = 1.07e+09
T4 p=0.3 n=40: size 10, 1017 calls, 2^n = 1.10e+12
T4 p=0.3 n=50: size 11, 3128 calls, 2^n = 1.13e+15
T4 p=0.3 n=60: size 13, 6357 calls, 2^n = 1.15e+18
T4 p=0.5 n=20: size 5, 60 calls, 2^n = 1.05e+06
T4 p=0.5 n=30: size 6, 320 calls, 2^n = 1.07e+09
T4 p=0.5 n=40: size 7, 644 calls, 2^n = 1.10e+12
T4 p=0.5 n=50: size 7, 1328 calls, 2^n = 1.13e+15
T4 p=0.5 n=60: size 8, 2719 calls, 2^n = 1.15e+18
```

At `n = 60`, `p = 0.1` there are `1.15e18` subsets and the search makes about 5,000 calls. Nothing here contradicts hardness. The graphs are random, so degrees are low and the independent set is small, and the rules fire often. Adversarial or structured graphs make the tree larger, and the worst case stays exponential. (Better branching rules give bases well below 2, around 1.2 to 1.4 in the literature, from memory.) It is also why you ask the interviewer what the input looks like.

Same idea on a size where both finish, 22 vertices, `p = 0.3`:

```
T9 22 vertices, p=0.3: exhaustive search 8, branching 8 with 92 calls (2^22 = 4194304 subsets)
```

| Method | Time (ms), run 1 / run 2 |
|---|---|
| all `2^22` subsets | 2.9 / 3.1 |
| branching | 0.001 / 0.001 |

The branching time is about a microsecond, so the printed 0.001 ms is a rough value; the point is the ratio of thousands.

## 5. The travelling salesman problem

Given `n` cities and all distances, find the shortest tour that visits each once and returns. It is the standard example of everything in this tutorial: NP-hard, exactly solvable for small `n`, approximable under extra assumptions, and well served by heuristics.

### 5.1 Brute force and Held-Karp

Fix city 0 and try every order of the others: `(n-1)!` tours. Held-Karp is the subset DP: `dp[S][j]` is the shortest path that starts at city 0, visits exactly the cities in `S`, and ends at `j`. There are `2^(n-1) * (n-1)` states and each has `n` moves, so `O(2^n * n^2)`. This is *the* answer to "can you do better than `n!`?".

```cpp
using Dist = vector<vector<double>>;
struct Pt { double x, y; };
inline Dist distances(const vector<Pt>& p) {
    Dist d(p.size(), vector<double>(p.size(), 0));
    for (size_t i = 0; i < p.size(); ++i) for (size_t j = 0; j < p.size(); ++j) d[i][j] = std::hypot(p[i].x - p[j].x, p[i].y - p[j].y);
    return d;
}
inline double tour_length(const Dist& d, const vector<int>& t) {
    double s = 0;
    for (size_t i = 0; i < t.size(); ++i) s += d[t[i]][t[(i + 1) % t.size()]];
    return s;
}

// Exact, factorial: every order of cities 1..n-1 (city 0 is fixed). n <= 10.
inline double tsp_brute(const Dist& d) {
    int n = (int)d.size();
    if (n <= 1) return 0;
    vector<int> t(n); std::iota(t.begin(), t.end(), 0);
    double best = std::numeric_limits<double>::infinity();
    do { best = std::min(best, tour_length(d, t)); } while (std::next_permutation(t.begin() + 1, t.end()));
    return best;
}

// Exact, exponential (Held-Karp): dp[S][j] = the shortest path from city 0 that visits exactly the cities in S
// (a subset of 1..n-1) and ends at j. 2^n * n^2 steps instead of n!.
inline double tsp_held_karp(const Dist& d) {                // n <= 20
    int n = (int)d.size();
    if (n <= 1) return 0;
    int m = n - 1;
    const double INF = std::numeric_limits<double>::infinity();
    vector<double> dp((size_t(1) << m) * m, INF);
    for (int j = 0; j < m; ++j) dp[((size_t)1 << j) * m + j] = d[0][j + 1];
    for (size_t S = 1; S < (size_t(1) << m); ++S)
        for (int j = 0; j < m; ++j) {
            double cur = dp[S * m + j];
            if (cur == INF) continue;
            for (int k = 0; k < m; ++k) {
                if (S >> k & 1) continue;
                double& to = dp[(S | (size_t(1) << k)) * m + k];
                to = std::min(to, cur + d[j + 1][k + 1]);
            }
        }
    double best = INF;
    for (int j = 0; j < m; ++j) best = std::min(best, dp[((size_t(1) << m) - 1) * m + j] + d[j + 1][0]);
    return best;
}

// Approximation 1 (needs the triangle inequality): build a minimum spanning tree, visit it in preorder.
// The tour is at most twice the tree, and the tree is at most the optimal tour (remove one edge from it).
inline vector<int> tsp_mst_tour(const Dist& d) {
    int n = (int)d.size();
    vector<int> parent(n, -1), order, st{0};
    vector<double> key(n, std::numeric_limits<double>::infinity());
    vector<char> in(n, 0);
    vector<vector<int>> kids(n);
    key[0] = 0;
    for (int it = 0; it < n; ++it) {
        int u = -1;
        for (int v = 0; v < n; ++v) if (!in[v] && (u < 0 || key[v] < key[u])) u = v;
        in[u] = 1;
        if (parent[u] >= 0) kids[parent[u]].push_back(u);
        for (int v = 0; v < n; ++v) if (!in[v] && d[u][v] < key[v]) { key[v] = d[u][v]; parent[v] = u; }
    }
    while (!st.empty()) {
        int u = st.back(); st.pop_back();
        order.push_back(u);
        for (auto it = kids[u].rbegin(); it != kids[u].rend(); ++it) st.push_back(*it);
    }
    return order;
}

// Heuristic: nearest neighbour (no guarantee), then 2-opt local search (reverse a stretch while that shortens the tour).
inline vector<int> tsp_nearest_neighbour(const Dist& d) {
    int n = (int)d.size();
    vector<int> t{0};
    vector<char> used(n, 0);
    used[0] = 1;
    for (int i = 1; i < n; ++i) {
        int last = t.back(), best = -1;
        for (int v = 0; v < n; ++v) if (!used[v] && (best < 0 || d[last][v] < d[last][best])) best = v;
        used[best] = 1; t.push_back(best);
    }
    return t;
}
inline void two_opt(const Dist& d, vector<int>& t) {
    int n = (int)t.size();
    for (bool improved = true; improved;) {
        improved = false;
        for (int i = 0; i + 2 < n; ++i)
            for (int j = i + 2; j < n; ++j) {
                if (i == 0 && j == n - 1) continue;
                int a = t[i], b = t[i + 1], c = t[j], e = t[(j + 1) % n];
                if (d[a][c] + d[b][e] < d[a][b] + d[c][e] - 1e-12) { std::reverse(t.begin() + i + 1, t.begin() + j + 1); improved = true; }
            }
    }
}
```

(The same code file holds the approximations of 5.2 and 5.3. They are shown here in one block so the file compiles as a unit.)

```
T5 nine random cities: brute force 2.789303, Held-Karp 2.789303
```

| Method | Time (ms), run 1 / run 2 |
|---|---|
| brute force, 9 cities | 1.1 / 1.0 |
| brute force, 10 cities | 10.6 / 10.0 |
| Held-Karp, 10 cities | 0.07 / 0.07 |
| Held-Karp, 14 cities | 2.25 / 2.06 |
| Held-Karp, 16 cities | 11.5 / 9.7 |
| Held-Karp, 18 cities | 55.0 / 55.1 |

Brute force grows by a factor of 9 or 10 per city (10 cities take about 9 times 9 cities), as `(n-1)!` says; at 13 cities it would be about 10 ms times 10 times 11 times 12, roughly 14 seconds, and every extra city multiplies that again. Held-Karp is already faster at 10 cities, takes 50 ms at 18, and grows by about 2.2 per city between 14 and 18. Its memory is the limit: 18 cities need about 18 MB, 20 about 80 MB, 25 about 3 GB (arithmetic on `2^(n-1) * (n-1)` doubles). So the exact method stops around 20 to 25 cities.

### 5.2 A 2-approximation

An **approximation algorithm** has a proven bound on how far it can be from optimal. For TSP with distances that satisfy the **triangle inequality** (the direct route is never longer than a detour; true for points in the plane):

1. Build a minimum spanning tree (MST).
2. Visit the cities in the order a depth-first walk first reaches them (preorder).

The tour is at most **twice the optimum**. Why: removing one edge from the optimal tour leaves a spanning tree, so the MST costs at most the optimum. A full walk around the tree uses every edge twice, so it costs at most twice the MST. Skipping the cities already visited (a shortcut) cannot lengthen the walk because of the triangle inequality, and that skipped walk is the preorder tour. The tests confirm `tour <= 2 * optimum` on thousands of random instances. (Christofides' algorithm improves the factor to 1.5, and without the triangle inequality no constant factor is possible unless P equals NP; both are from memory.)

### 5.3 Heuristics with no guarantee

**Nearest neighbour** walks to the closest unvisited city. **2-opt** then repeatedly takes two edges of the tour and reconnects them the other way (reversing the stretch between) whenever that shortens the tour, until no such swap helps. Neither has a useful worst-case bound, but they are fast and, as you will see, good.

How close are they? 300 random instances of 12 cities each, tour length divided by the Held-Karp optimum:

```
T6 300 random instances of 12 cities, tour length divided by the optimum (average / worst): MST preorder 1.176 / 1.420; nearest neighbour 1.118 / 1.461; nearest neighbour then 2-opt 1.010 / 1.157
```

The guaranteed method is *worse in practice* than the plain heuristic: the 2-approximation averages 18 percent above optimal and reached 42 percent above, though it can never reach 100. Nearest neighbour is closer on average (12 percent) but its worst case is 46 percent above and it has no bound. Adding 2-opt brings the average to 1 percent above optimal, with a worst case of 16 percent. This is the usual pattern: the guarantee protects the worst case; local search gives better typical results. In real use (from memory) people run a good local search such as Lin-Kernighan, or an integer-programming or constraint solver, not Held-Karp, once the cities number in the hundreds or more.

## 6. Special structure: when the hard part is small

Hardness is a statement about the whole input. If some *parameter* of the instance is small, the problem can be fast.

### 6.1 Vertex cover with a small parameter

Is there a vertex cover of size at most `k`? Take any edge not covered yet. One of its two ends must be in the cover. Try the first, and if that fails, the second. The recursion has depth `k` and branching factor 2, so at most `2^(k+1)` calls, **whatever the size of the graph**. Such an algorithm is called fixed-parameter tractable (FPT): exponential only in the parameter.

```cpp
// A small parameter makes it tractable. Is there a vertex cover with at most k vertices?
// Take any edge not yet covered: one of its two ends must be in the cover. Branch on which. Depth k, so at most
// 2^(k+1) calls, whatever the size of the graph. (Fixed-parameter tractable: O(2^k * m).)
inline bool vertex_cover_fpt(const vector<std::pair<int, int>>& edges, vector<char>& in_cover, int k, long long& calls) {
    ++calls;
    const std::pair<int, int>* e = nullptr;
    for (const auto& x : edges) if (!in_cover[x.first] && !in_cover[x.second]) { e = &x; break; }
    if (!e) return true;
    if (k == 0) return false;
    for (int v : {e->first, e->second}) {
        in_cover[v] = 1;
        if (vertex_cover_fpt(edges, in_cover, k - 1, calls)) return true;
        in_cover[v] = 0;
    }
    return false;
}

// NP-hard in the length of the input, but tractable when the numbers are small: pseudo-polynomial time O(n * target).
inline bool subset_sum_dp(const vector<long long>& a, long long target) {
    if (target < 0) return false;
    vector<char> can(target + 1, 0);
    can[0] = 1;
    for (long long x : a) {
        if (x > target) continue;
        for (long long s = target; s >= x; --s) if (can[s - x]) can[s] = 1;   // downwards: each number used once
    }
    return can[target];
}
```

A graph with 200 vertices and 72 edges, each edge touching one of 12 planted vertices:

```
T7 200 vertices, 72 edges, every edge touches one of 12 chosen vertices; cover of size at most 11: none exists, 4095 calls (bound 2^(k+1) = 4096)
T7 200 vertices, 72 edges, every edge touches one of 12 chosen vertices; cover of size at most 12: found, 13 calls (bound 2^(k+1) = 8192)
T7 trying every set of 12 vertices instead: C(200,12) = 6.11e+18 sets
```

Proving that no cover of size 11 exists took 4,095 calls (the bound is 4,096) in a fraction of a millisecond. Trying every set of 12 vertices would be `6e18` sets. This is no contradiction: the algorithm is exponential in `k`, and fast only because `k` is small.

### 6.2 Subset sum with small numbers

Given numbers and a target, is there a subset summing to the target? NP-complete when the numbers are written in binary. But the table `can[s]` (is sum `s` reachable?) has `target + 1` cells and is filled by one pass per number: `O(n * target)`. The inner loop runs **downward** so that each number is used once (running upward would let a number be used again, which solves a different problem; a deliberate bug of that kind was among the mutants and was caught).

```
T8 subset sum, 100 numbers up to 100,000, target 2500000: reachable yes; the table has 250000100 cells; trying every subset would take 2^100 = 1.27e+30
T8 the same problem with 60-bit numbers: the target is near 2^60 = 1.15e+18 and the table would have about 1.15e+20 cells
```

| Work | Time (ms), run 1 / run 2 |
|---|---|
| subset sum DP, 100 numbers up to 100,000, target 2,500,000 | 89.7 / 96.0 |

250 million cells in about a tenth of a second, against `2^100` subsets. This is **pseudo-polynomial** time: polynomial in the *value* of the target, exponential in the *number of bits* needed to write it. If the numbers have 60 bits, the table has about `1e20` cells and the method is useless. Same for 0/1 knapsack. So "knapsack is NP-hard" and "I solved a knapsack with capacity 10,000 instantly" are both true.

### 6.3 Other ways in (from memory)

- **SAT and constraint solvers.** Modern SAT, SMT and integer-programming solvers solve many large industrial instances of NP-complete problems (millions of variables). Encoding a hard problem into them is a legitimate engineering answer.
- **Special graph classes.** Many NP-hard graph problems are polynomial on trees, bipartite graphs, or graphs of bounded treewidth (tree DP).
- **Randomisation and approximation schemes** for problems such as knapsack are the subject of the next tutorial, 14.02.

## 7. What to say in an interview

1. **Notice the pattern.** "Choose a subset", "visit every exactly once", "partition into equal groups", "satisfy all constraints" with no ordering structure are the usual smell.
2. **Name it once.** "This looks like it reduces from Hamiltonian path, so I expect it to be NP-hard." One sentence. Do not try a proof.
3. **Ask for the size.** If `n` is 20 or less, subsets and bitmask DP are the intended answer. If it is 1000, an exact method is not expected.
4. **Choose among the responses:** exact search with pruning; DP over subsets (`2^n * n` or `2^n * n^2`); an approximation with a ratio you can state; a heuristic you can measure; or an FPT or pseudo-polynomial algorithm if a parameter is small.
5. **State the guarantee you give.** Exact and exponential; or "at most twice optimal if the triangle inequality holds"; or "usually near optimal, no bound".
6. **Do not claim a polynomial algorithm** for an NP-complete problem unless you are ready to claim a prize.

## 8. Complexities

| Method | Time | Space |
|---|---|---|
| Verify a Hamiltonian cycle | `O(n)` | `O(n)` |
| Hamiltonian cycle, subset DP | `O(2^n * n)` | `O(2^n)` |
| Independent set, all subsets | `O(2^n * n)` | `O(1)` |
| Independent set, branching | exponential, below `2^n` (output dependent) | `O(n)` stack |
| TSP brute force | `O(n!)` | `O(n)` |
| TSP, Held-Karp | `O(2^n * n^2)` | `O(2^n * n)` |
| TSP, MST preorder | `O(n^2)` (Prim, dense) | `O(n)` |
| TSP, nearest neighbour | `O(n^2)` | `O(n)` |
| TSP, 2-opt | no polynomial bound proved here; fast in the runs above | `O(n)` |
| Vertex cover with parameter `k` | `O(2^k * m)` | `O(k)` stack |
| Subset sum DP | `O(n * target)` | `O(target)` |

## 9. Common mistakes

1. **Reducing in the wrong direction.** Reduce a known hard problem *to yours*.
2. Confusing "NP" with "not polynomial", or "NP-hard" with "in NP".
3. Calling a problem NP-hard without checking it is not the tractable twin (a path problem that is really a shortest path).
4. Announcing "dynamic programming" for the longest simple path without noticing the state needs the set of visited vertices.
5. Using the 2-approximation of TSP on distances that break the triangle inequality: the bound is gone.
6. **Subset sum DP loops running upward** (a number used many times), or a table indexed by a negative number.
7. Saying subset sum or knapsack is "polynomial" because of the DP. It is pseudo-polynomial.
8. Branching on an arbitrary vertex and not removing the safe ones first, so the tree is far larger.
9. In Held-Karp, forgetting that city 0 is fixed (a factor of `n` more states) or forgetting the closing edge back to city 0.
10. Treating a heuristic's good result on one test as a guarantee.
11. Reading an assignment off a 3-SAT reduction without checking it satisfies the formula.

## 10. Questions to answer out loud

1. Define NP with a certificate. Why is every problem in P also in NP?
2. Why does the Hamiltonian cycle checker run in `O(n)` while the finder is exponential? What does that gap mean?
3. Give three pairs of problems where one is in P and its twin is NP-hard. What changed in each?
4. You must show problem X is NP-hard. Which way does the reduction go?
5. Explain why `alpha + tau = n`. How do clique and independent set relate?
6. Walk through the 3-SAT to independent set construction. Why a triangle? Why the edges between `x` and `not x`?
7. Why can a vertex of degree 1 always be taken in a maximum independent set?
8. How does Held-Karp beat `n!`? What does it cost in memory?
9. Why is the MST preorder tour at most twice the optimum? Which assumption does it need?
10. The 2-approximation did worse than nearest neighbour here. Why would you still care about it?
11. What does "pseudo-polynomial" mean? Why is subset sum NP-complete and still solved by an `O(n * target)` table?
12. Vertex cover with parameter `k` runs in `2^k * m`. Why is that not polynomial, and when is it good enough?
13. Given a hard problem with 30 items, what do you do? With 100,000?

## 11. Problems

### 11.1 On paper

**A.** A graph on 7 vertices with edges 0-1, 1-2, 2-0, 2-3, 3-4, 4-5, 5-3, 5-6 (two triangles joined by an edge, with a tail). Find the maximum independent set, the minimum vertex cover, the maximum clique, and the independent number of the complement.

**B.** The formula `(x1 or x2 or x3) and (not x1 or not x2 or not x3) and (x1 or not x2 or x3)`. Build the independent-set graph of section 3.2: how many vertices and edges does it have, and what is its largest independent set? Is the formula satisfiable?

**C.** How many cells does Held-Karp fill for `n` = 10, 16 and 20 (counting `(n-1) * 2^(n-1)`)? Compare with `(n-1)!`.

**D.** The vertex-cover search with `k = 20` on a graph of 1000 vertices. Give the largest number of calls it can make, and compare with trying every set of 20 vertices.

**E.** Subset sum on `{3, 34, 4, 12, 5, 2}`: is 9 reachable? Is 30? How many cells does the table have for target 9?

**F.** For each, say whether it is known to be in P, or NP-hard, or tractable only for small numbers: (i) shortest path with some negative edges and no negative cycle; (ii) longest simple path in a general graph; (iii) 2-SAT; (iv) deciding whether a graph can be coloured with 3 colours; (v) finding an Euler circuit; (vi) 0/1 knapsack with 100 items and capacity 10,000.

### 11.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. All of these have small limits, which is the hint that an exponential method is intended.

| Problem | Number | How to approach |
|---|---|---|
| Subsets | 78 | The base case: all `2^n` subsets by bitmask or backtracking |
| Partition Equal Subset Sum | 416 | Subset sum with target = half the total; the downward table of 6.2 |
| Partition to K Equal Sum Subsets | 698 | NP-hard in general; backtracking with a sorted order, or bitmask DP over used items |
| Shortest Path Visiting All Nodes | 847 | A TSP-like state `(visited set, current node)` with BFS; Held-Karp's state space |
| Find the Shortest Superstring | 943 | TSP on overlaps; Held-Karp over subsets of words |
| Minimum Number of Work Sessions | 1986 | Bin packing with 14 tasks; DP over subsets |
| Smallest Sufficient Team | 1125 | Set cover with 16 skills; DP over the mask of covered skills |
| Maximum Score Words Formed by Letters | 1255 | Choose a subset of at most 14 words; backtracking with letter counts |

### 11.3 In your editor

**A.** Write a **branch and bound** TSP: extend a partial tour city by city and cut a branch when its length plus a lower bound (for example an MST of the unvisited cities) already exceeds the best tour. Compare the number of nodes with Held-Karp's states on 12 to 16 cities.

**B.** Write a **DPLL SAT solver** (unit propagation plus branching). Test against a brute-force check over all assignments for 12 variables, then try random 3-SAT with the clause-to-variable ratio near 4.3, where instances get hard (from memory).

**C.** Write a backtracking **3-colouring** with the most constrained vertex first. Test on random graphs and measure the nodes visited.

**D.** Add a greedy upper bound to the branching independent-set search (for example the number of remaining vertices minus a matching) and measure how much it prunes.

**E.** Solve subset sum by **meet in the middle** (split into two halves, enumerate each half's sums, match with sorting): `O(2^(n/2))`. This is the subject of 14.03; try it now for `n` = 40 and compare with the table method on big numbers.

### Answers

The program that checks the paper problems printed:

```
A alpha=3 (set: 0 3 6 ) tau=4 omega=3 alpha_of_complement=3
B vertices=9 edges=15 alpha=3 clauses=3
C n=10 held-karp cells=4608 orders (n-1)!=3.629e+05
C n=16 held-karp cells=491520 orders (n-1)!=1.308e+12
C n=20 held-karp cells=9961472 orders (n-1)!=1.216e+17
D k=20 calls<=2097151  C(1000,20)=3.395e+41
E target9=1 target30=0 cells for target9=60
```

**A.** The graph has two triangles {0,1,2} and {3,4,5}, a bridge 2-3 and a tail 5-6. The maximum independent set has size 3 (for example 0, 3 and 6), so the minimum vertex cover has size `7 - 3 = 4`. The maximum clique is a triangle, size 3. The independent number of the complement is therefore also 3.

**B.** There are 9 vertices. The three triangles give 9 edges, and the edges between a literal and its negation in different clauses give 6 more: 3 between clauses 1 and 2, 1 between clauses 1 and 3 (`x2` and `not x2`), and 2 between clauses 2 and 3 (`not x1` against `x1`, `not x3` against `x3`). Total 15 edges. The largest independent set has size 3, equal to the number of clauses, so the formula is satisfiable. (For example `x1` false, `x2` false, `x3` true satisfies it.)

**C.** The cells number `(n-1) * 2^(n-1)`: 4,608 for `n` = 10, 491,520 for 16, 9,961,472 for 20. The orders `(n-1)!` number `3.6e5`, `1.3e12`, `1.2e17`. At `n` = 10 Held-Karp fills about 80 times fewer cells, at 16 millions of times fewer, at 20 about ten billion times fewer.

**D.** At most `2^(k+1) - 1 = 2,097,151` calls whatever the graph, against `C(1000, 20)`, about `3.4e41`, sets.

**E.** 9 is reachable (4 + 5, or 3 + 4 + 2). 30 is not: the numbers that fit under 30 add up to only 26, and 34 is too large. For target 9 the table has 10 cells per pass over 6 numbers, so 60 cell updates.

**F.** (i) In P: Bellman-Ford. (ii) NP-hard: it contains Hamiltonian path. (iii) In P: strongly connected components of the implication graph. (iv) NP-complete (from memory). (v) In P: check that every vertex has even degree in a connected graph, then follow Hierholzer's method. (vi) Pseudo-polynomial: the DP over capacity, 100 times 10,000 cells, is fast; the problem is still NP-hard in general.

## Cheat sheet

- **NP:** yes answers have certificates checkable in polynomial time. **NP-complete:** in NP and every NP problem reduces to it. Checking a Hamiltonian cycle: 30 ns; finding one: 54 ms at 20 dense vertices, 256 to 272 ms at 22.
- **Reduction direction:** reduce a known hard problem *to yours*.
- **Independent set, vertex cover, clique:** `alpha + tau = n`; `omega(G) = alpha(complement)`. 3-SAT to independent set: triangle per clause, edge between a literal and its negation; satisfiable if and only if an independent set of size `m` exists.
- **Small `n`:** subsets `2^n * n`; branching with safe reductions (calls: about 5,000 against `1e18` at `n` = 60, on random graphs); TSP by Held-Karp `2^n * n^2` (18 cities in about 50 ms, against brute force at 10 cities in about 10 ms).
- **Approximate:** metric TSP, MST preorder, factor 2 (measured average 1.18, worst 1.42); nearest neighbour 1.12 average, worst 1.46; with 2-opt 1.01, worst 1.16.
- **Small parameter:** vertex cover `2^k * m` (4,095 calls to refute `k` = 11 on 200 vertices). **Small numbers:** subset sum `O(n * target)`, pseudo-polynomial, loop downward.
- **Say in an interview:** name the hard problem, ask the size, state the guarantee you give.

---

## Sources

- All code is tested here (1,144,519 checks under ASan and UBSan; 19 deliberate bugs all caught, one only by timeout). The traces and timings are measured on one Xeon container, two runs each, with identical traces. The graphs and city sets are random and seeded.
- The theory (definitions of P, NP and NP-completeness, Cook and Levin's theorem, the table of tractable and hard twins, the factor 1.5 of Christofides, the impossibility of a constant factor for general TSP, the better exponents for independent set, the hardness of random 3-SAT near ratio 4.3, the strength of modern SAT solvers, the NP-completeness of 3-colouring) is standard material and was written from memory; none of it was checked against a source during this work. The reduction from 3-SAT, the identities for independent set, the MST bound and the call bound of the cover search are my own derivations and were tested.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
