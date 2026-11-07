# DSA 08.04: BFS on States and Grids

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Graph representations and traversal](01_representations_and_traversal.md) (BFS, mark on push, grids as graphs) and [Connectivity and cycles](02_connectivity_and_cycles.md) (grid regions). There is no companion demo file: every structure is listed in this document.*

Topic 01 said BFS finds the fewest edges from a source. This topic is about the observation that turns that sentence into a large family of interview problems: **the vertices do not have to be places**. A vertex can be a lock combination, a board position, a word, "I am at cell (3, 4) and have broken two walls", or "I am at node 5 and have visited this set of nodes". The edges are the legal moves, and "fewest moves" is a BFS. Nothing about the search changes; what you design is the **state** (what must be remembered so the future depends on nothing else), the **moves**, and a bound on how many states there are. Then four refinements: start from many sources at once, search from both ends, handle edges that cost 0 or 1, and steer by an estimate (A*).

By the end you can:

- design a state for a "fewest moves" puzzle and bound the size of the state space before writing code;
- run multi-source BFS and explain why one pass beats one search per source;
- solve the lock, sliding-puzzle and word-ladder problems with one generic implicit-graph BFS, and recognise when one search from the goal answers every start;
- add information to the state (a visited set as a bitmask, a budget of wall breaks) and count what it costs;
- write bidirectional BFS and say, with measurements, when it wins by a factor of hundreds and when it does nothing;
- write 0-1 BFS with a deque, and A* with a consistent heuristic, and say when A* is slower than plain BFS.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 28,447 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: multi-source distances against one BFS per source, rotting oranges against a minute-by-minute simulation; the lock against a separate search over strings with random dead ends, and against the closed formula (the sum over the four wheels of `min(d, 10 - d)`) with none; the sliding puzzle for **all 720** arrangements against a separate search over strings; word ladders against pairwise comparison; visit-all-nodes against Floyd-Warshall plus a dynamic programme over subsets; wall breaking against repeated relaxation over the (cell, breaks) states; bidirectional BFS against plain BFS on random graphs including disconnected ones and `s == t`; 0-1 BFS against Dijkstra; A* against BFS (same distance, never more expansions) and with a weight that must sometimes return a longer path; and a path of a million vertices and grids of millions of cells. Then 75 hand-made mutants of the listings were run against the tests: **65 were caught and 10 are equivalent changes** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: grids with a given percentage of walls placed at random; a random graph with V = 1,000,000 vertices and 3 million random edges. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Work counts and answers are identical in both runs.

---

## 1. Thinking in states

Every problem in this topic is solved by answering three questions before any code.

1. **What is a state?** The smallest description of "where I am" that determines which moves are legal and what the goal needs. If two situations with the same description can behave differently, the state is too small (you will get wrong answers). If it carries more than needed, the search is slower than necessary.
2. **What are the moves?** For each state, the neighbouring states and what a move costs. BFS needs every move to cost the same.
3. **How many states are there?** This is the budget. A BFS visits each state at most once, so the work is states times moves per state. Compute it first: 10^4 states is nothing, 4.6 * 10^4 is nothing, 10^8 is a problem, and a state that includes a set of 30 things is 2^30 states.

Compare with Topic 01: there the graph was given. Here it is **implicit**, never built; the neighbour function is code. The generic search below is the whole interface.

## 2. Multi-source BFS

Problem: for every cell, the distance to the nearest of several sources (the nearest gate, the nearest rotten orange, the nearest land cell). Running BFS from each source and taking the minimum costs sources times grid. The trick is to put **all sources in the queue at distance 0** before the first step.

```cpp
// Multi-source BFS: put EVERY source in the queue at distance 0 before the first step. The queue then holds the vertices in order of
// distance to the NEAREST source, so one pass gives the distance to the nearest source for every cell. '#' is a wall, and the sources
// are the cells equal to `src`. dist is -1 for a cell no source can reach (and for walls).
std::vector<int> grid_dist(const std::vector<std::string>& g, char src) {
    int H = g.size(), W = H ? g[0].size() : 0; std::vector<int> dist(H * W, -1), q;
    for (int i = 0; i < H * W; ++i) if (g[i / W][i % W] == src) { dist[i] = 0; q.push_back(i); }
    static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h];
        for (int k = 0; k < 4; ++k) {
            int r = u / W + DR[k], c = u % W + DC[k];
            if (r >= 0 && r < H && c >= 0 && c < W && g[r][c] != '#' && dist[r * W + c] < 0) { dist[r * W + c] = dist[u] + 1; q.push_back(r * W + c); }
        }
    }
    return dist;
}
// Rotting oranges: '0' empty, '1' fresh, '2' rotten. Each minute every fresh orange next to a rotten one rots.
// Returns the minute when none is fresh, or -1 if some fresh orange is never reached. It is multi-source BFS with the answer = the largest distance.
int rotting(const std::vector<std::string>& g) {
    std::vector<std::string> w = g; for (auto& row : w) for (char& ch : row) ch = ch == '0' ? '#' : ch;       // empty cells are walls for the search
    std::vector<int> d = grid_dist(w, '2'); int H = g.size(), W = H ? g[0].size() : 0, best = 0;
    for (int i = 0; i < H * W; ++i) if (g[i / W][i % W] == '1') { if (d[i] < 0) return -1; best = std::max(best, d[i]); }
    return best;
}
```

**Why it is correct.** Add one imaginary vertex joined to every source by an edge of length 0 (or 1, and subtract 1 at the end). A single BFS from it reaches every cell at the distance to its nearest source; the code is that BFS with the imaginary vertex already expanded. The queue still holds vertices in order of distance, which is the only property BFS needs.

**Measured.** A 1000 by 1000 grid with 15 percent walls and 100 random sources. One pass takes 35.4 / 36.3 ms; 100 separate searches (one per source, merged by minimum) take 2,525.7 / 2,641.8 ms, 71 to 73 times as long. Both give the same distances, and the farthest cell is 188 steps from its nearest source. The factor is a little below the number of sources (100): each separate search still touches the whole reachable grid.

Rotting oranges is the same search with the answer being the largest distance over the fresh oranges, and `-1` if any fresh orange was not reached. The same shape solves "01 Matrix" (sources are the zeros), "As Far from Land as Possible" (sources are the land) and "Shortest Bridge" (sources are one whole island found by a flood fill, Topic 02).

```text
multi-source distances (-1 = wall or unreachable)
 0  1  2 -1  2
 1 -1  3 -1  1
 2  3  4 -1  0
-1  4  3  2  1
rotting A 4
rotting B -1
```

The first grid has two sources `S`. The cell at row 0, column 4 is at distance 2 from the source in row 2, column 4 (up through column 4). Walls and the cell they cut off show `-1`. The last two lines are the classic rotting examples: the first grid finishes after 4 minutes; in the second, the orange at the bottom left is cut off by empty cells, so the answer is `-1`.

## 3. Implicit graphs: the lock, the sliding puzzle, the word ladder

```cpp
// A state graph that is never stored: states are numbered 0 .. N-1 and next(u, emit) calls emit(v) for every neighbour of u.
// The search is the BFS of Topic 01; only the neighbour rule is new, and "the vertices" are whatever you decide is a state.
template <class Next>
std::vector<int> bfs_implicit(int N, int src, Next next) {
    std::vector<int> dist(N, -1), q{src}; dist[src] = 0;
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h];
        next(u, [&](int v) { if (dist[v] < 0) { dist[v] = dist[u] + 1; q.push_back(v); } });
    }
    return dist;
}
// Open the lock: four wheels 0-9, each turn moves one wheel by one (9 and 0 are neighbours); start "0000"; some combinations are dead.
// The state is the four-digit number itself: 10000 states, 8 neighbours each. Returns the fewest turns to `target`, or -1.
int open_lock(const std::vector<std::string>& dead, const std::string& target) {
    std::vector<char> blocked(10000, 0); for (auto& d : dead) blocked[std::stoi(d)] = 1;
    int t = std::stoi(target); if (blocked[0] || blocked[t]) return -1;
    auto dist = bfs_implicit(10000, 0, [&](int u, auto emit) {
        for (int p = 1; p <= 1000; p *= 10) for (int delta : {1, 9}) {
            int digit = u / p % 10, v = u - digit * p + (digit + delta) % 10 * p;
            if (!blocked[v]) emit(v);
        }
    });
    return dist[t];
}
// Sliding puzzle 2 x 3: tiles 1..5 and a blank (0); a move slides a tile into the blank. A board is a number in base 6 (6^6 = 46656 codes,
// of which 720 are permutations). The goal is [[1,2,3],[4,5,0]]. Moves are reversible, so ONE search from the goal gives the answer for
// every starting board at once.
std::vector<int> sliding_table() {
    int goal = 0; const int G[6] = {1, 2, 3, 4, 5, 0}; for (int i = 0; i < 6; ++i) goal = goal * 6 + G[i];
    return bfs_implicit(46656, goal, [&](int u, auto emit) {
        int cell[6], z = 0, x = u; for (int i = 5; i >= 0; --i) { cell[i] = x % 6; x /= 6; if (cell[i] == 0) z = i; }
        static const int NB[6][3] = {{1, 3, -1}, {0, 2, 4}, {1, 5, -1}, {0, 4, -1}, {1, 3, 5}, {2, 4, -1}};
        for (int k = 0; k < 3; ++k) {
            int p = NB[z][k]; if (p < 0) break;
            std::swap(cell[z], cell[p]); int v = 0; for (int i = 0; i < 6; ++i) v = v * 6 + cell[i]; std::swap(cell[z], cell[p]); emit(v);
        }
    });
}
int sliding_puzzle(const std::vector<std::vector<int>>& b) {
    static const std::vector<int> table = sliding_table(); int code = 0;
    for (auto& row : b) for (int x : row) code = code * 6 + x;
    return table[code];
}
// Word ladder: change one letter at a time, every intermediate word must be in the list. Words are the states; to find the neighbours
// of "hot" without comparing it with every word, file each word under its patterns "*ot", "h*t", "ho*": words sharing a pattern differ
// in at most that one position. Returns the number of words in the shortest ladder (begin and end included), or 0.
int word_ladder(const std::string& begin, const std::string& end, const std::vector<std::string>& words) {
    std::vector<std::string> w = words; w.push_back(begin); int n = w.size(), start = n - 1, goal = -1;
    for (int i = 0; i < n - 1; ++i) if (w[i] == end) goal = i;
    if (begin == end) return 1;
    if (goal < 0) return 0;
    std::unordered_map<std::string, std::vector<int>> bucket;
    for (int i = 0; i < n; ++i) for (size_t p = 0; p < w[i].size(); ++p) { std::string key = w[i]; key[p] = '*'; bucket[key].push_back(i); }
    auto dist = bfs_implicit(n, start, [&](int u, auto emit) {
        for (size_t p = 0; p < w[u].size(); ++p) { std::string key = w[u]; key[p] = '*'; for (int v : bucket[key]) emit(v); }
    });
    return dist[goal] < 0 ? 0 : dist[goal] + 1;
}
```

`bfs_implicit` is the BFS of Topic 01 with the graph replaced by a function: `next(u, emit)` calls `emit(v)` for each neighbour. States are integers `0 .. N-1`, so `dist` is a plain array, which is faster than a hash map and is why each listing below spends effort on encoding its state as a small integer.

**Open the lock.** A state is the four-digit number (10,000 states), a move turns one wheel by one in either direction (8 neighbours), and dead ends are states you refuse to enter. Two quick exits: if `0000` is itself dead, or the target is, the answer is `-1`. With no dead ends the wheels are independent, so the answer is the sum over the wheels of `min(d, 10 - d)` (the tests confirm this formula), which makes `5555` the farthest target at 20 turns. Dead ends are what make the search necessary.

**Sliding puzzle.** A state is the arrangement of the tiles, written in base 6 (a number below 46,656). There are 720 arrangements of six items but the code indexes a table of 46,656. A move slides a tile into the blank, so the blank's position decides the moves (2 or 3 neighbours per cell, from the table `NB`). **Moves are reversible**, so the distance from a board to the goal equals the distance from the goal to the board, and **one BFS from the goal** fills a table that answers every possible starting board: `sliding_puzzle` is a lookup. That trick works whenever moves can be undone.

```text
lock A 6
lock B -1
lock C -1
lock D 20
sliding A 1
sliding B -1
sliding C 5
sliding boards reachable 360 of 720
sliding histogram 1 2 3 5 6 7 10 12 12 16 23 25 28 39 44 40 29 21 18 12 6 1
ladder 5
ladder none 0
```

The first four lines are the lock: the standard example with five dead ends needs 6 turns; when all eight neighbours of `8888` are dead the target is walled off and the answer is `-1`; a dead `0000` gives `-1`; and `5555` takes 20. Then the sliding puzzle. Only **360 of the 720** arrangements can reach the goal (the other 360 form a second, unreachable half: the invariant is in problem B), and the farthest solvable board needs **21** moves. The histogram lists how many boards are at distance 0, 1, 2, ... 21: one board at distance 0 and 1 at distance 21, and the counts sum to 360. The three examples: one move; the unreachable arrangement `[[1,2,3],[5,4,0]]` gives `-1`; the arrangement `[[4,1,2],[5,0,3]]` takes 5.

**Word ladder.** The states are words and two words are neighbours when they differ in exactly one position. Comparing every pair of words is quadratic. Instead file each word under its **patterns** (`hot` goes under `*ot`, `h*t`, `ho*`): two words differing only in one position share a pattern, so the neighbours of a word are the other words in its L buckets. Building the buckets costs (number of words) * L * L (string copies) and the search touches each bucket entry once. Three details in the code: the **begin word need not be in the list** (it is appended as the start), the **end word must be in the list** (otherwise the answer is 0), and the result counts **words**, not changes (hence the `+ 1`). `hit` to `cog` through `hot dot dog cog` or `hot lot log cog` is 5 words; remove `cog` from the list and no ladder exists.

## 4. Putting more into the state

### 4.1 A set of things done: the bitmask

"Shortest walk that visits every node of a small graph": being at node 5 is not enough, because what you may do next depends on **what you have already visited**. The state is `(mask of visited nodes, current node)`: n * 2^n states, each with deg(node) moves. Start from all `(1 << v, v)` at once (multi-source, section 2), and stop at the first state whose mask is full. Edges and nodes may be reused, which is why this is BFS and not a Hamiltonian path search.

```cpp
// State = (where I am) + (what I have done so far). "Shortest walk that visits every node of a small graph": the state is
// (set of visited nodes as a bitmask, current node), n * 2^n states, and the search starts from ALL (1 << v, v) at once.
// The walk may revisit nodes and reuse edges. Returns the number of edges of the shortest such walk, or -1 if the graph is disconnected.
int shortest_visit_all(const std::vector<std::vector<int>>& adj) {
    int n = adj.size(); if (n <= 1) return 0; int full = (1 << n) - 1;
    std::vector<int> dist((size_t)n << n, -1), q;
    for (int v = 0; v < n; ++v) { dist[((size_t)1 << v) * n + v] = 0; q.push_back((1 << v) * n + v); }
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h], mask = u / n, at = u % n;
        if (mask == full) return dist[u];
        for (int nb : adj[at]) { int s = (mask | 1 << nb) * n + nb; if (dist[s] < 0) { dist[s] = dist[u] + 1; q.push_back(s); } }
    }
    return -1;
}
// State = (cell, number of walls broken so far): H * W * (k + 1) states. '1' is a wall, '0' open; from the top-left to the bottom-right
// with 4 moves; entering a wall costs one of the k breaks. Returns the fewest steps or -1.
int grid_k_walls(const std::vector<std::string>& g, int k) {
    int H = g.size(), W = g[0].size(), K = k + 1; int first = g[0][0] == '1'; if (first > k) return -1;
    std::vector<int> dist((size_t)H * W * K, -1), q; dist[first] = 0; q.push_back(first);
    static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h], cell = u / K, used = u % K;
        if (cell == H * W - 1) return dist[u];
        for (int d = 0; d < 4; ++d) {
            int r = cell / W + DR[d], c = cell % W + DC[d]; if (r < 0 || r >= H || c < 0 || c >= W) continue;
            int nu = used + (g[r][c] == '1'); if (nu > k) continue;
            int s = (r * W + c) * K + nu; if (dist[s] < 0) { dist[s] = dist[u] + 1; q.push_back(s); }
        }
    }
    return -1;
}
```

For n = 12 there are 49,152 states; for n = 20 it is 21 million. That bound is the reason the problem statement says `n <= 12`. A star with a centre and three leaves needs 4 steps (leaf, centre, leaf, centre, leaf), a path of four vertices needs 3 (both in the trace below).

### 4.2 A budget: breaking walls

"Shortest path from the top-left to the bottom-right corner, you may break at most `k` walls": the position alone is not a state, because the same cell reached with 0 breaks left is a better situation than reached with none. The state is `(cell, breaks used)`: H * W * (k + 1) states. A search with the state `(cell)` would mark a cell visited the first time and could never come back with a smaller budget in hand; BFS over the larger state space never has that problem, because it distinguishes the situations.

```text
visit all star 4
visit all path 3
walls k=0 10 k=1 6 k=2 6
```

On the 5 by 3 grid with two wall rows, `k = 0` needs a 10-step detour; `k = 1` cuts it to 6; `k = 2` gains nothing more (the shortest path is already the Manhattan distance, 6). The state counts for a 100 by 100 grid are 10,000 for k = 0, 60,000 for k = 5 and 510,000 for k = 50. The answer stops improving once `k` reaches the number of cells a Manhattan path can cross (problem C), which is a cheap way to cap `k` before allocating.

Both searches are the same shape: **the state is the position plus whatever the future depends on**. Bitmask states appear again in the DP topic (the travelling salesman programme is this state with costs); budget states appear in problems with fuel, time limits and "at most k changes".

## 5. Bidirectional BFS

If the branching factor is `b` and the distance is `d`, a one-sided search reaches about `b^d` states, while two searches that each go half way reach about `2 b^(d/2)`: the square root of the work. The method is to grow a ball around each end, always expanding the **smaller** frontier by one whole level, and to stop at the first edge that joins the two balls.

```cpp
// Bidirectional BFS on an undirected graph: grow a ball around s and a ball around t, always expanding the smaller frontier by one whole
// level, and stop at the first edge that joins the two balls. Before that moment the balls had no vertex and no edge in common, so the
// distance is at least a + b + 1 for balls of radii a and b, and the joining edge gives exactly a + 1 + b: the first meeting is the answer.
// `visited` counts the (ball, vertex) pairs reached, the work, which is the point of the method.
struct Csr { int n = 0; std::vector<int> off, to; };
Csr build_csr_undirected(int n, const std::vector<std::pair<int, int>>& e) {
    Csr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto [a, b] : e) { ++g.off[a + 1]; ++g.off[b + 1]; }
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];
    g.to.resize(2 * e.size()); std::vector<int> at(g.off.begin(), g.off.end() - 1);
    for (auto [a, b] : e) { g.to[at[a]++] = b; g.to[at[b]++] = a; }
    return g;
}
struct BidirResult { int dist = -1; long long visited = 0; };
BidirResult bidir_bfs(const Csr& g, int s, int t) {
    BidirResult r; if (s == t) { r.dist = 0; r.visited = 1; return r; }
    std::vector<int> ds(g.n, -1), dt(g.n, -1), fs{s}, ft{t}, next; ds[s] = 0; dt[t] = 0; r.visited = 2;
    while (!fs.empty() && !ft.empty()) {
        bool fwd = fs.size() <= ft.size();
        std::vector<int>& f = fwd ? fs : ft; std::vector<int>& mine = fwd ? ds : dt; std::vector<int>& other = fwd ? dt : ds;
        next.clear();
        for (int u : f) for (int i = g.off[u]; i < g.off[u + 1]; ++i) {
            int v = g.to[i];
            if (other[v] >= 0) { r.dist = mine[u] + 1 + other[v]; return r; }
            if (mine[v] < 0) { mine[v] = mine[u] + 1; next.push_back(v); ++r.visited; }
        }
        f.swap(next);
    }
    return r;
}
```

Why the first meeting is exact. The balls grow by **whole levels** and are always the complete sets of vertices within distance `a` of s and within `b` of t. While they share no vertex, `dist(s, t) >= a + b + 1`: a shortest path would have its first `a` edges inside the first ball and its last `b` inside the second, and if it were any shorter they would overlap. The moment the search finds an edge from a vertex `u` of the frontier being expanded (depth `a`) to a vertex of the other ball (depth at most `b`), it has a path of at most `a + 1 + b` edges. It cannot be shorter than `a + b + 1`, so it is exactly that, and it is a shortest path. No minimum over several meetings is needed. (I first wrote the code with such a minimum and with "finish the level"; a search over 2.2 million small random graphs found no input where the first meeting differed from the minimum, and the argument above says why.) The graph must be undirected, or the backward ball must walk the reversed edges. `visited` is the work: it counts a vertex once for each ball that reaches it.

**Measured.** 200 random vertex pairs on a random graph with 10^6 vertices and 3 * 10^6 edges (average degree 6, all pairs connected):

| Search | Vertices reached per query | ms per query (run 1 / run 2) |
|---|---|---|
| Plain BFS | 823,825 | 48.8 / 49.4 |
| Bidirectional | 2,086 | 0.5 / 0.5 |

The work drops by a factor of 395, and the time by about 104 to 105 times. The tree trace: on a complete binary tree of 15 nodes, from leaf 7 to leaf 14 the distance is 6 and the two balls together reached 10 (ball, vertex) pairs (a tiny example for the mechanics, not a measurement).

```text
bidir tree 7 to 14: dist 6 visited 10
```

**It is not always a win.** The same code on an open 2000 by 2000 grid from one corner to the opposite corner reached 4,000,000 (ball, vertex) pairs, the same as the 4,000,000 vertices of plain BFS, and was slower (137.5 / 151.7 ms against 82.6 / 65.8 ms). On a grid the balls grow like areas (`d^2`), not like `b^d`, and in a bounded square the two balls together cover about the whole grid anyway; the halving of the radius is cancelled by the geometry. The method pays off when the number of states reachable in `k` steps **multiplies** with `k` (random graphs, word ladders, puzzle states) and not when it grows polynomially.

## 6. 0-1 BFS

When every move costs 0 or 1, you do not need Dijkstra's heap. Keep the queue as a **deque**: relax a free move by pushing the new vertex to the **front**, a costly move to the **back**. The deque then always has at most two distinct distances, `d` at the front and `d + 1` behind, in order, which is exactly what BFS needs, and each relaxation is O(1).

```cpp
// 0-1 BFS: shortest paths when every edge costs 0 or 1. Dijkstra would need a heap; here the queue is a deque, a free move goes to the
// FRONT and a costly move to the BACK, which keeps the deque sorted by distance with at most two distinct values in it.
// Arrows: cell values 1 right, 2 left, 3 down, 4 up. Leaving a cell along its own arrow is free, any other direction costs 1 (changing the
// arrow). Returns the cheapest total from the top-left to the bottom-right corner.
int min_arrow_changes(const std::vector<std::vector<int>>& a) {
    int H = a.size(), W = a[0].size(); std::vector<int> dist(H * W, 1 << 30); std::deque<int> dq{0}; dist[0] = 0;
    static const int DR[4] = {0, 0, 1, -1}, DC[4] = {1, -1, 0, 0};                    // direction d has arrow value d + 1
    while (!dq.empty()) {
        int u = dq.front(); dq.pop_front();
        for (int d = 0; d < 4; ++d) {
            int r = u / W + DR[d], c = u % W + DC[d]; if (r < 0 || r >= H || c < 0 || c >= W) continue;
            int w = a[u / W][u % W] != d + 1, nd = dist[u] + w, v = r * W + c;
            if (nd < dist[v]) { dist[v] = nd; if (w) dq.push_back(v); else dq.push_front(v); }
        }
    }
    return dist[H * W - 1];
}
```

The example is "make at least one valid path": every cell has an arrow, following it is free, and changing an arrow costs 1. A vertex can be pushed more than once (once when it is reached at cost `d + 1`, again if a free route finds `d`), so the test is `nd < dist[v]`, not "unvisited".

```text
arrows A 3
arrows B 1
arrows C 0
```

These are the standard three examples: 3 changes, 1 change, and 0 (the arrows already form a path). **Measured**: on a 2000 by 2000 grid of random arrows (answer 1,049 changes) 0-1 BFS takes 115.8 / 117.7 ms and a heap-based Dijkstra on the same grid takes 448.1 / 382.8 ms, 3.2 to 3.9 times as long, with the same answer.

## 7. A*: BFS with a compass

Plain BFS spreads in every direction. If you know roughly which way the goal is, you can expand cells in order of `f = steps so far + estimated steps left`. That is A*. The estimate `h` must be **admissible** (never more than the true remaining distance) for the answer to be shortest, and the simplest safe way to meet the stronger **consistency** condition (`h` drops by at most 1 per step) is to use a distance that obeys the triangle inequality, such as Manhattan distance on a 4-move grid. With a consistent `h`, a cell taken from the heap has its final distance, so the first time the goal is taken the answer is exact and no cell is ever re-opened.

```cpp
// A*: BFS that expands the most promising cell first, ordered by f = (steps so far) + h, where h estimates the steps left.
// With h = Manhattan distance on a 4-move grid, h never overestimates (admissible) and never drops by more than 1 per step (consistent),
// so the first time the goal is taken from the heap its distance is exact. weight > 1 multiplies h: faster, but the answer may be too long.
// `expanded` counts the cells taken from the queue (for A*, from the heap), the usual measure of search effort.
struct SearchResult { int dist = -1; long long expanded = 0; };
SearchResult astar_grid(const std::vector<std::string>& g, int sr, int sc, int tr, int tc, int weight = 1) {
    int H = g.size(), W = g[0].size(); SearchResult r;
    if (g[sr][sc] == '#' || g[tr][tc] == '#') return r;
    auto h = [&](int id) { return weight * (std::abs(id / W - tr) + std::abs(id % W - tc)); };
    std::vector<int> best(H * W, 1 << 30); std::vector<char> closed(H * W, 0);
    using Item = std::tuple<int, int, int>;                                          // (f, -g, cell): among equal f, the deeper cell first
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq; int s = sr * W + sc, t = tr * W + tc; best[s] = 0; pq.push({h(s), 0, s});
    static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
    while (!pq.empty()) {
        int u = std::get<2>(pq.top()); pq.pop(); if (closed[u]) continue;
        closed[u] = 1; ++r.expanded; if (u == t) { r.dist = best[u]; return r; }
        for (int d = 0; d < 4; ++d) {
            int rr = u / W + DR[d], cc = u % W + DC[d]; if (rr < 0 || rr >= H || cc < 0 || cc >= W || g[rr][cc] == '#') continue;
            int v = rr * W + cc, ng = best[u] + 1; if (ng < best[v]) { best[v] = ng; pq.push({ng + h(v), -ng, v}); }
        }
    }
    return r;
}
// The baseline: plain BFS on the same grid, counting a cell as expanded when it is taken from the queue, stopping when the goal is taken.
SearchResult bfs_grid(const std::vector<std::string>& g, int sr, int sc, int tr, int tc) {
    int H = g.size(), W = g[0].size(); SearchResult r;
    if (g[sr][sc] == '#' || g[tr][tc] == '#') return r;
    std::vector<int> dist(H * W, -1), q{sr * W + sc}; dist[sr * W + sc] = 0; int t = tr * W + tc;
    static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
    for (size_t hd = 0; hd < q.size(); ++hd) {
        int u = q[hd]; ++r.expanded; if (u == t) { r.dist = dist[u]; return r; }
        for (int d = 0; d < 4; ++d) {
            int rr = u / W + DR[d], cc = u % W + DC[d];
            if (rr >= 0 && rr < H && cc >= 0 && cc < W && g[rr][cc] != '#' && dist[rr * W + cc] < 0) { dist[rr * W + cc] = dist[u] + 1; q.push_back(rr * W + cc); }
        }
    }
    return r;
}
```

Ties in `f` are broken towards the **deeper** cell (`-g` in the tuple): it makes the search dive towards the goal instead of sweeping the whole `f` level. `weight > 1` is "weighted A*": it trusts the estimate more, expands less, and gives up the guarantee.

```text
open 9x9: dist 16 bfs_expanded 81 astar_expanded 17
wall 9x9: dist 12 bfs_expanded 61 astar_expanded 29
weighted x3: dist 12 expanded 29
one wall: bfs 8 astar 8 weighted x3 10
```

On an open 9 by 9 grid plain BFS expands all 81 cells and A* expands 17, exactly the cells of the path. With the wall, A* expands 29 against 61. The last two lines are the warning: on a 5 by 5 grid with **one** wall at row 3, column 4 the true distance from corner to corner is 8, and weighted A* with weight 3 returns 10.

**Measured** on 2000 by 2000 grids from corner to corner (cells expanded, and ms, run 1 / run 2):

| Walls | Path | BFS expanded | A* expanded | BFS ms | A* ms |
|---|---|---|---|---|---|
| 0% | 3,998 steps | 4,000,000 | 3,999 | 81.1 / 83.1 | 9.6 / 8.1 |
| 20% | 3,998 steps | 3,192,183 | 268,725 | 107.1 / 152.3 | 45.7 / 50.7 |
| 30% | none | 2,750,438 | 2,750,438 | 141.4 / 126.5 | 480.2 / 486.3 |

A* expanded far fewer cells in the first two rows and the same number in the last (the tests found no case, over thousands of random grids, in which it expands more), and it won on time in the first two rows. In the last row **no path exists**: A* has no way to know that and must exhaust the component like BFS, but pays a heap operation per cell; it is about 3.4 to 3.8 times slower. A* is an optimisation for **reachable** goals with a good estimate; when the goal may be unreachable, check connectivity first (Topic 02), or accept the cost. A* is Dijkstra with a head start, and Dijkstra is Topic 05.

## 8. Choosing

| Situation | Use |
|---|---|
| Distance to the nearest of many sources | Multi-source BFS, one pass |
| Fewest moves in a puzzle, small state space | `bfs_implicit` with an integer-coded state |
| Reversible moves, many starting positions | One BFS from the goal; answer by lookup |
| The future depends on a set or a budget | Put it in the state (mask, count) and bound the product |
| Huge branching, both ends known, undirected | Bidirectional BFS |
| Moves cost 0 or 1 | 0-1 BFS with a deque |
| A good distance estimate, goal reachable | A* with a consistent heuristic |
| Moves cost anything else | Dijkstra (Topic 05) |

## 9. What the tests check, and what they cannot

Of the 75 mutants, 65 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Three of the 65 survived the first run and are caught only because of tests I then added: the "smaller frontier first" rule of the bidirectional search (a test graph where the wrong side costs ten thousand extra vertices), the exact number of cells A* expands on a 9 by 9 grid, and the exact cost of an unreachable goal. Ten survive, and each is an equivalent change as far as the answers go: the mutated program returns the same answers as the original on every input. The reasons:

- **Dropping `blocked[t]` from the lock's early exit.** A dead target is never entered, so `dist[t]` stays -1 by itself. (`blocked[0]` is different: the start is not "entered", so it needs its own test.)
- **`{2, 4, 5}` instead of `{2, 4, -1}` in the sliding table.** Cell 5 listed as its own neighbour swaps the blank with itself and emits the state it is already in, which is already visited.
- **`continue` instead of `break` on a `-1` neighbour.** The `-1` entries are always last in a row of the table.
- **Looping over `n` instead of `n - 1` words when looking for the end word.** The extra word is the appended begin word, and `begin == end` has already returned.
- **Skipping `v == u` in the word-ladder neighbours.** A word is its own bucket-mate, and itself is already visited.
- **`dist[goal] + 1` without the unreachable test.** `-1 + 1` is 0, which is the answer for "no ladder".
- **Pushing every 0-1 BFS vertex to the back.** The search becomes a plain queue-based relaxation that re-processes some vertices; it is slower but the answers are the same.
- **`h + 1` in A\*.** A constant added to every `f` leaves the order of the heap unchanged.
- **`ng <= best[v]` in A\*.** An equal-cost duplicate goes into the heap and is then discarded by the `closed` test.
- **`pq.push({0, 0, s})` for the start.** The start is the only entry in the heap when it is first taken, so its key does not matter.

I wrote these as plausible slips and classed them as equivalent by reading the code, not because the tests passed.

Mutants are chosen where a bug is plausible, not at random, so "65 of 75" is a statement about those edits and not a proof about the listings. The state spaces tested exhaustively are small (all 720 boards, graphs of up to 7 nodes for the bitmask, grids of up to 5 by 5 for the wall budget); the large tests check answers that can be computed in closed form. Timings depend on the machine.

## 10. Common mistakes

- **A state that is too small.** Marking a cell visited when the same cell with a different budget or set is a different situation (wall breaking, keys and doors).
- Marking visited on pop instead of on push (Topic 01): the queue fills with duplicates.
- Forgetting that the start can already be a goal, a dead end, or a wall.
- Word ladder: not appending the begin word, treating the end word as optional, returning the number of changes instead of words, comparing every pair of words.
- Bidirectional BFS: expanding the larger frontier (it still works, but loses the saving); one shared distance array for both balls (you cannot then tell the meeting from a revisit); using it on a directed graph without the reverse edges.
- 0-1 BFS: pushing a free move to the back, or marking visited at the first push.
- A*: a heuristic that overestimates (weight above 1, or a distance that ignores the 4-move rule); re-using the heuristic of one problem on another.
- Not bounding the state space first and finding out at 10^9.
- Using recursion for a search over 10^6 states.
- Treating "A* is faster" as a fact: it is faster only when the goal is reachable and the estimate is informative.

## 11. Questions to answer out loud

1. **What is a state and how do you choose one?** The least information that determines the legal moves and the goal test; add to it only what the future depends on.
2. **How do you bound the work?** States times moves per state, computed before writing code.
3. **Why does multi-source BFS work?** It is BFS from an imaginary vertex joined to every source.
4. **How much does it save?** About the number of sources over separate searches (measured: 71 to 73 times for 100 sources).
5. **When does one BFS from the goal answer everything?** When moves are reversible (the sliding puzzle).
6. **Word ladder in one sentence?** States are words; neighbours come from wildcard buckets; the answer counts words.
7. **Why is `(cell)` not enough with a wall budget?** The same cell with a larger remaining budget is a better situation.
8. **How many states in the visit-all problem?** n * 2^n.
9. **Why does bidirectional BFS help?** The two balls hold `2 b^(d/2)` instead of `b^d` states when the ball grows exponentially.
10. **When does it not help?** On a grid corner to corner (measured: no reduction); in general, when the ball grows polynomially.
11. **How does 0-1 BFS work?** A deque; free moves to the front, costly ones to the back; at most two distances in it.
12. **What makes A* optimal, and when is it slower than BFS?** An admissible and consistent estimate; when the goal is unreachable it still expands everything and pays for the heap.

## 12. Problems

### 12.1 On paper

**A.** Without dead ends, why is the distance from `0000` to a lock target the sum over the wheels of `min(d, 10 - d)`? What is the farthest target and its distance?

**B.** The 2 by 3 sliding puzzle has 720 arrangements but only 360 reach the goal. Find an invariant that separates them, and use it to show `[[1,2,3],[5,4,0]]` is unreachable.

**C.** For a grid of H by W, with the start and the target open, what is the smallest `k` after which `grid_k_walls` always returns the Manhattan distance? For H = W = 100 and k = 50, how many states does the search have?

**D.** Prove that the deque of 0-1 BFS never holds more than two distinct distance values, and that they appear in order.

**E.** Prove that multi-source BFS gives, for every vertex, the distance to its nearest source.

**F.** A graph has average degree 6 and two vertices at distance 8. Estimate the number of vertices a one-sided BFS and a bidirectional BFS reach, ignoring overlap and the size of the graph.

### 12.2 On LeetCode

For each: what is the state, what are the moves, and which of this topic's tools? Hints are in the answers; try first. Numbers and titles are from memory; as far as I recall none of these is premium.

| Problem | Number | What to practise |
|---|---|---|
| Rotting Oranges | 994 | Multi-source BFS, answer = largest distance |
| 01 Matrix | 542 | Sources are the zeros |
| As Far from Land as Possible | 1162 | Sources are the land cells, answer = the largest distance |
| Shortest Bridge | 934 | Flood fill one island (Topic 02), then multi-source BFS from it |
| Open the Lock | 752 | Section 3 |
| Sliding Puzzle | 773 | Section 3, base-6 or string states |
| Word Ladder | 127 | Section 3, wildcard buckets |
| Word Ladder II | 126 | All shortest ladders: BFS levels, then walk back |
| Minimum Genetic Mutation | 433 | A word ladder with a four-letter alphabet |
| Snakes and Ladders | 909 | States are squares; a roll is up to six moves; careful numbering |
| Shortest Path Visiting All Nodes | 847 | Section 4.1 |
| Shortest Path to Get All Keys | 864 | State (cell, key mask) |
| Shortest Path in a Grid with Obstacles Elimination | 1293 | Section 4.2 |
| Minimum Cost to Make at Least One Valid Path in a Grid | 1368 | Section 6 |

### 12.3 In your editor

**H.** Add parent pointers to `bfs_implicit` and print the shortest sequence of lock combinations from `0000` to a target. Test the printed path move by move.

**I.** Solve "Shortest Path to Get All Keys": the state is (cell, set of keys held), a door needs its key. Count the states for a 30 by 30 grid with 6 keys.

**J.** Replace the heuristic in `astar_grid` by the Euclidean distance rounded down and by the Chebyshev distance, and check on random grids which of them still returns the shortest distance on a 4-move grid and which expands fewer cells.

### Answers

**A.** The wheels are independent: each turn changes one wheel by one, and a wheel needs `min(d, 10 - d)` turns to go from 0 to digit `d` (up or down, wrapping). The total is the sum, which is also the lower bound for any target. The farthest target is `5555`: 5 + 5 + 5 + 5 = 20.

**B.** Write the tiles in reading order without the blank and count the inversions (pairs in the wrong order). Sliding a tile sideways does not change that sequence. Sliding it vertically moves it past exactly two other tiles in the sequence, which changes the inversion count by -2, 0 or +2. So the **parity** of the inversion count never changes. The goal `1 2 3 4 5` has 0 inversions (even). `[[1,2,3],[5,4,0]]` reads `1 2 3 5 4`, with one inversion (odd), so it can never reach the goal.

**C.** A Manhattan path from corner to corner visits H + W - 1 cells, of which H + W - 3 are neither the start nor the target. So such a path crosses at most H + W - 3 walls, and for `k >= H + W - 3` the answer is always the Manhattan distance, H + W - 2 steps. For H = W = 100, k = 50: 100 * 100 * 51 = 510,000 states.

**D.** Induction. Initially the deque holds the source at distance `d`. Pop the front `u` (distance `d`). A free move pushes a vertex at distance `d` to the front; a costly one pushes `d + 1` to the back. Everything in the deque was at distance `d` or `d + 1` and in order (all `d` before all `d + 1`); after the pushes that is still true. When the `d` entries are used up, the front is `d + 1` and the same argument repeats.

**E.** Add a vertex `z` with an edge of length 0 to each source. A shortest path from `z` to any vertex starts with one such edge and then is a shortest path from some source, so its length is the distance to the nearest source. BFS from `z` already has `z` taken off the queue and every source in it at distance 0, which is the code.

**F.** One-sided: about 6^8 = 1,679,616 vertices (the graph would cap it). Bidirectional: two balls of radius 4, about 2 * 6^4 = 2,592. The measurement above (random graph, 10^6 vertices) is 823,825 against 3,583, the same order of magnitude in the right direction; the graph's size and the overlaps of the balls explain the difference.

**LeetCode hints.** 994: sources are the 2s; fresh oranges not reached mean -1. 542: sources are all 0 cells, the answer is the distance array. 1162: sources are the land cells, answer = largest distance, or -1 if all land or all water. 934: find one island by a flood fill, put all its cells in the queue, expand until you meet the other island. 752: section 3. 773: the table of section 3, or strings in a hash set. 127: section 3; the answer is words, so +1. 126: BFS recording the parents of each word at its first level, then walk back from the end. 433: as 127 with letters `ACGT`. 909: number the squares along the boustrophedon, state = square, six moves per state, a ladder or snake is taken immediately. 847: section 4.1. 864: state (r, c, key mask), a door is passable if its key bit is set. 1293: section 4.2. 1368: section 6.

**H.** Keep `parent[v] = u` at the first discovery and walk back from the target. **I.** The state count is 30 * 30 * 2^6 = 57,600; the BFS visits at most that many. **J.** Euclidean (rounded down) and Chebyshev are both admissible on a 4-move grid (they never exceed Manhattan), so both return the shortest distance, but they are weaker estimates, so they expand more cells than Manhattan. Measured over 240 random 60 by 60 grids with 20 percent walls (all reachable), the total cells expanded were 92,881 for Manhattan, 621,303 for floor-Euclidean, 623,137 for Chebyshev and 688,795 for plain BFS: the weaker estimates are barely better than no estimate.

---

## Cheat sheet

- **State:** the least information that fixes the legal moves and the goal test; count the states before coding (states times moves per state).
- **Multi-source:** put all sources in the queue at distance 0; one pass instead of one per source (71 to 73 times less time for 100 sources, measured).
- **Implicit graph:** `bfs_implicit(N, src, next)`; encode the state as an integer; reversible moves mean one BFS from the goal answers all starts.
- **Bigger states:** (cell, budget) or (node, visited mask): n * 2^n or H * W * (k + 1) states.
- **Bidirectional:** expand the smaller frontier by whole levels, stop at the first meeting; 395 times fewer vertices on a random graph, no gain on a grid (measured).
- **0-1 BFS:** deque, free to the front, costly to the back; 3.2 to 3.9 times faster than Dijkstra here.
- **A*:** `f = g + h`, consistent `h` (Manhattan); fewer expansions, but a heap per cell and 3.4 to 3.8 times slower than BFS when the goal is unreachable (measured).

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The facts that a 2 by 3 sliding puzzle has 360 reachable arrangements and a farthest solvable board at 21 moves were computed by the code (and checked against a separate search); the inversion-parity argument is the standard one. A* and its admissibility and consistency conditions are standard (Hart, Nilsson and Raphael, 1968, named from memory); 0-1 BFS is a well-known folklore technique.
- Problem numbers and titles in section 12 are LeetCode's, from memory; check a number before you rely on it.
