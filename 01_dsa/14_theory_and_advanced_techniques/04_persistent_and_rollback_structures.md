# DSA 14.04: Persistent and Rollback Structures

*Part of the [Theory and advanced techniques plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: segment trees from [Trees](../07_trees/00_plan.md), union-find from [Graphs](../08_graphs/00_plan.md), coordinate compression from [Square-root techniques, offline queries and meet in the middle](03_sqrt_offline_and_meet_in_the_middle.md), and the time-based structures of [Design-style coding](../13_design_style_coding/00_plan.md)*

Most data structures forget their past: an update overwrites the old state. This tutorial is about two ways of **keeping or recovering** the past. A **persistent** structure keeps every version readable at the same time, by sharing most of its memory between versions. A **rollback** structure keeps only the present but can undo its latest changes in the reverse order, by logging how to reverse each change. They answer different questions. "What did the array look like at snapshot 7, and what if I change it from there?" needs persistence. "Process a long list of edge additions and removals offline, with connectivity queries in between" needs rollback. Both show up as interview problems (a snapshot array, a key-value store with transactions, a range k-th smallest) and as the machinery of senior-level algorithms.

By the end you can:

- say what a version is, and distinguish partial persistence (read any version, change only the newest) from full persistence (change any version);
- build a snapshot array by **fat nodes**: one list of changes per index, found by binary search (measured memory against full copies);
- build a persistent array by **path copying**, and say why each update costs `O(log n)` new nodes and `O(log n)` time (measured);
- answer "k-th smallest in a subarray" with a persistent segment tree over values, one version per prefix;
- write a union-find with **rollback** (no path compression, union by size) and say why;
- solve **offline dynamic connectivity** by putting each edge into a segment tree over time and walking it with rollback;
- implement a key-value store with nested **transactions** by an undo log, and compare its cost with copying the whole map;
- choose between persistence, rollback and copying, and say what each costs.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 38,391,286 checks passed under AddressSanitizer and UBSan. Every structure is compared with a plain oracle: full copies of the array for every version, a sort of the range, recomputing components from scratch, BFS at every query, copies of the whole map, and full copies for every snapshot. Invariants are tested directly (number of nodes created per update, tree depth, the component sizes kept by the union-find after rollbacks). I tried 21 deliberate bugs. All were caught, but one escaped the first tests and I added a check for it: a rollback that forgets to restore the component size changes only how balanced later unions are, not who is connected. Three of the transaction bugs were caught because the program crashed, not because of a wrong answer.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Data is random from a seeded generator, so every run gives the same counts. Memory figures count nodes times their size (12 bytes per node here), not what the allocator really uses. Statements about theory (the formal classes of persistence, bounds known for other structures) and LeetCode are from memory and marked.

---

## 1. Versions and the ways to keep them

A **version** is the state of the structure after some operation. An **ephemeral** structure keeps only the latest. A **persistent** one lets you read any older version, and in the **fully persistent** case also update any older version, which creates a branch (a tree of versions). A third kind, **confluent** persistence, also allows merging two versions (from memory; not covered here). Four ways to get there, in increasing cleverness:

| Method | Idea | Cost per update | Memory |
|---|---|---|---|
| Copy | Copy the whole structure for each version | `O(n)` | `O(n)` per version |
| Fat nodes | Each element keeps a list of (version, value) | `O(1)` or `O(log n)` | `O(changes)` |
| Path copying | Copy only the nodes on the path to the changed leaf; share the rest | `O(log n)` | `O(log n)` per version |
| Rollback | Keep only the present; log how to undo; undo in reverse order | `O(1)` plus the log | `O(changes since the mark)` |

Persistence needs **random access to versions**. Rollback needs only a **stack of versions**, which is enough whenever the versions are visited in a depth-first order (offline problems). That difference is the main design decision: if you can reorder the work into a depth-first walk over versions, rollback is smaller and faster; if you must jump between versions in arbitrary order, you need persistence.

## 2. Fat nodes: a snapshot array

The task (it is LeetCode 1146, number from memory): an array of length `n`, initially zeros, with `set(i, v)`, `snap()` (returns a snapshot id) and `get(i, id)` (the value of index `i` when snapshot `id` was taken). Copying the array at each `snap()` costs `n` per snapshot. With 100,000 indices and 100,000 snapshots that is 10 billion integers.

Fat nodes: for each index keep a list of `(snapshot id, value)` pairs, one for each snapshot period in which the index was written. `set` appends or overwrites the last pair. `get` binary-searches for the last pair whose id is at most the one asked for. Memory is proportional to the number of writes.

```cpp
// A snapshot array by fat nodes: for every index keep the list of (snapshot id, value) pairs, one per snapshot in which it changed.
// snap() is O(1) and set() is O(1); get(i, id) is a binary search for the last change at or before snapshot id.
// Memory is proportional to the number of changes, not to (length * number of snapshots).
struct SnapshotArray {
    vector<vector<pair<int, int>>> h;
    int next_id = 0;
    explicit SnapshotArray(int n) : h(n, vector<pair<int, int>>{{0, 0}}) {}
    void set(int i, int v) {
        if (h[i].back().first == next_id) h[i].back().second = v; else h[i].push_back({next_id, v});
    }
    int snap() { return next_id++; }                  // returns the id of the snapshot just taken
    int get(int i, int id) const {                    // value of index i as it was when snapshot id was taken
        const auto& l = h[i];
        auto it = std::upper_bound(l.begin(), l.end(), std::make_pair(id, 2147483647));
        return (it - 1)->second;
    }
    size_t entries() const { size_t s = 0; for (const auto& l : h) s += l.size(); return s; }
};
```

```
T6 array of 100000, 100000 snapshots with 300000 writes in total: 399993 stored entries (1.00 per write after the 100000 initial ones); 10000 of 10000 reads from 50 saved full copies agree; keeping a full copy per snapshot would need 100000 * 100000 ints = 40 GB
```

300,000 writes over 100,000 snapshots created 399,993 stored entries (the 100,000 initial ones, plus one per write, minus a few writes that overwrote one made in the same period). A full copy per snapshot would need 40 GB. The answers were checked against 50 full copies saved at the time. A million random reads took:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| 1,000,000 `get` calls | 51.6 / 44.6 |

Fat nodes suit structures where each element changes independently. For a structure where an update touches a path (a tree), path copying is the right tool.

## 3. Path copying: a persistent array

Store the array as a balanced binary tree whose leaves are the elements. To change element `i`, copy the root, then the child on the way to `i`, and so on down to the leaf: `log2(n) + 1` new nodes. Every other node is shared with the old version. The new root is the new version. **Nothing is ever changed in place**, so old roots stay valid, and updating an old version works exactly like updating the newest one.

```cpp
// A persistent array by path copying. The array is a balanced binary tree whose leaves hold the values.
// set() never changes an existing node: it copies the nodes on the path from the root to the leaf (O(log n) of them)
// and returns the NEW root. Every old root still describes the old array, so every version stays readable and writable.
struct PersistentArray {
    struct Node { int l, r, val; };                 // leaf: val is the value; inner node: l and r are indices into pool
    vector<Node> pool;
    int n, base;
    explicit PersistentArray(const vector<int>& init) : n((int)init.size()), base(0) { pool.reserve(2 * init.size() + 1); base = build(0, n, init); }
    int root0() const { return base; }               // the root of version 0
    int build(int lo, int hi, const vector<int>& init) {
        if (hi - lo == 1) { pool.push_back({-1, -1, init[lo]}); return (int)pool.size() - 1; }
        int mid = (lo + hi) / 2, l = build(lo, mid, init), r = build(mid, hi, init);
        pool.push_back({l, r, 0});
        return (int)pool.size() - 1;
    }
    int set(int root, int i, int v) { return set_rec(root, 0, n, i, v); }
    int set_rec(int node, int lo, int hi, int i, int v) {
        Node c = pool[node];                         // copy first: pool may reallocate during the recursion
        if (hi - lo == 1) c.val = v;
        else {
            int mid = (lo + hi) / 2;
            if (i < mid) c.l = set_rec(c.l, lo, mid, i, v); else c.r = set_rec(c.r, mid, hi, i, v);
        }
        pool.push_back(c);
        return (int)pool.size() - 1;
    }
    int get(int root, int i) const {
        int lo = 0, hi = n, node = root;
        while (hi - lo > 1) { int mid = (lo + hi) / 2; if (i < mid) { node = pool[node].l; hi = mid; } else { node = pool[node].r; lo = mid; } }
        return pool[node].val;
    }
};
```

(Notice that `set_rec` copies the node **before** recursing. `pool.push_back` can reallocate the vector, which would invalidate a reference taken earlier; a copy of the node is safe.)

A hand trace on `n = 4`, leaves `a b c d`, inner nodes `P = (a b)`, `Q = (c d)`, root `R = (P Q)`. `set(R, 2, x)` creates `c'` (the new leaf), `Q' = (c' d)` and `R' = (P Q')`. Three new nodes; `P`, `a`, `b` and `d` are shared. Version `R` is untouched and still reads `a b c d`; version `R'` reads `a b x d`.

Measured on an array of 65,536 elements, with 100,000 updates, each applied to a **random earlier version** (so the versions form a tree, which is full persistence):

```
T1 array of 65536, 100000 updates, each applied to a random earlier version: 131071 nodes after the build, 1831071 after the updates (17.0 per update); 21.0 MB; versions 0 to 300 match full copies at every 97th index: yes
T1 keeping a full copy of every version instead would need 100000 * 65536 * 4 bytes = 26.2 GB
```

Each update creates 17 nodes, as the depth of the tree says, and the whole forest of 100,001 versions takes 21 MB, against 26 GB for full copies. Time:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| 100,000 updates, building the tree of versions | 37.9 / 31.6 |
| 1,000,000 reads from random versions | 239.7 / 239.5 |

A read takes about 240 ns: the walk down 17 levels leaves cache and jumps through 21 MB of nodes. That is the price of persistence compared with an ordinary array read.

**Memory and old versions.** This code never frees nodes. In a real system you discard versions you no longer need, which requires reference counting or a garbage collector (from memory: functional languages do this for you, which is why persistent maps are their default). The simplest persistent structure of all is the linked list used as a stack: a push creates one node pointing to the old head, so every older version stays valid, with no copying at all.

## 4. A persistent segment tree: the k-th smallest in a range

Query: for a static array and many `(l, r, k)`, find the `k`-th smallest of `a[l..r]`. Sorting each range costs `O(n log n)` per query.

Make a segment tree over the **values** (after compression), where each node stores a count. Build one version per prefix: version `i` is the tree of the first `i` elements, made from version `i - 1` by path copying one insertion (`log V + 1` new nodes). The elements of `a[l..r]` are "version `r + 1` minus version `l`": the count of values in any value range inside the subarray is the difference of the counts in the two trees. To find the `k`-th smallest, walk down both trees at once: if the left child's count difference is at least `k`, go left; otherwise subtract it from `k` and go right.

```cpp
// k-th smallest in a subarray, with a persistent segment tree over VALUES (after coordinate compression).
// Version i is the tree of counts for the first i elements. The elements of a[l..r] are "version r+1 minus version l",
// so the count in any value range is the difference of two counts, and we can walk both trees down together.
struct KthInRange {
    struct Node { int l, r, cnt; };
    vector<Node> pool;
    vector<int> root;                                // root[i]: version holding a[0..i)
    vector<long long> vals;                          // sorted distinct values
    int V;
    explicit KthInRange(const vector<long long>& a) {
        vals = a;
        std::sort(vals.begin(), vals.end());
        vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
        V = (int)vals.size();
        pool.push_back({0, 0, 0});                   // node 0: the empty tree, its own children
        root.push_back(0);
        for (long long x : a) {
            int pos = (int)(std::lower_bound(vals.begin(), vals.end(), x) - vals.begin());
            root.push_back(insert(root.back(), 0, V, pos));
        }
    }
    int insert(int prev, int lo, int hi, int pos) {
        Node c = pool[prev];
        ++c.cnt;
        if (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            if (pos < mid) c.l = insert(c.l, lo, mid, pos); else c.r = insert(c.r, mid, hi, pos);
        }
        pool.push_back(c);
        return (int)pool.size() - 1;
    }
    // k-th smallest (k from 1) among a[l..r], inclusive; needs 1 <= k <= r - l + 1.
    long long kth(int l, int r, int k) const {
        int u = root[r + 1], v = root[l], lo = 0, hi = V;
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2, left = pool[pool[u].l].cnt - pool[pool[v].l].cnt;
            if (k <= left) { u = pool[u].l; v = pool[v].l; hi = mid; }
            else { k -= left; u = pool[u].r; v = pool[v].r; lo = mid; }
        }
        return vals[lo];
    }
};
```

```
T2 array of 200000 values up to 10^9 (199971 distinct): 3737816 nodes (18.7 per element), 42.8 MB; 300 of 300 queries checked against a selection on a copy of the range agree
```

| Work | Time (ms), run 1 / run 2 |
|---|---|
| build the 200,000 versions | 150.2 / 138.3 |
| 100,000 queries | 103.8 / 97.4 |
| 300 queries by copying the range and selecting (`nth_element`) | 129.9 / 129.3 |

The structure uses 3.7 million nodes (42.8 MB, about 19 per element, which is `log2(V) + 1`) and answers a query in about a microsecond. The selection on a copy took about 0.43 ms per query, so 100,000 queries by that method would take about 43 seconds (extrapolated from the 300). The same idea answers "number of values at most `x` in `a[l..r]`", "number of distinct values", and, with versions indexed by time instead of by prefix, queries about the past of a structure (from memory).

## 5. Union-find with rollback

The ordinary union-find compresses paths, which rewires many nodes at once and cannot be undone cheaply. For rollback, drop path compression and keep **union by size**: attach the smaller tree under the larger one. Every tree then has depth at most `log2 n` (a node's depth grows only when its tree at least doubles), so `find` is `O(log n)`. Each successful union changes only two things, the parent of one root and the size of the other, and the **history stack** records which. `rollback_to(s)` pops records until the stack has size `s` and undoes each: restore the parent, subtract the size, add one back to the component count.

```cpp
// Union-find with rollback. No path compression (it cannot be undone cheaply); union by size keeps every tree
// at depth O(log n), so find is O(log n). Every successful union is recorded, and rollback_to() removes the most recent ones.
struct RollbackDsu {
    vector<int> p, sz;
    vector<pair<int, int>> hist;                     // (root that was attached, root it was attached to)
    int comps;
    explicit RollbackDsu(int n) : p(n), sz(n, 1), comps(n) { for (int i = 0; i < n; ++i) p[i] = i; }
    int find(int x) const { while (p[x] != x) x = p[x]; return x; }
    bool unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return false;
        if (sz[a] < sz[b]) std::swap(a, b);
        p[b] = a; sz[a] += sz[b]; hist.push_back({b, a}); --comps;
        return true;
    }
    size_t snapshot() const { return hist.size(); }
    void rollback_to(size_t s) {
        while (hist.size() > s) {
            int b = hist.back().first, a = hist.back().second;
            hist.pop_back();
            sz[a] -= sz[b]; p[b] = b; ++comps;
        }
    }
};
```

```
T3 100000 vertices, 300000 random unions: 248 components, 99752 successful unions, deepest tree 7 (log2 n = 16.6); after rolling back to the start there are 100000 components
```

After 300,000 random unions on 100,000 vertices the deepest tree has depth 7 (the bound is 16.6), and 9 ms covers all 300,000 unions. Rolling back to the start restores 100,000 singleton components.

| Work | Time (ms), run 1 / run 2 |
|---|---|
| 300,000 random unions, 100,000 vertices | 9.1 / 9.8 |

## 6. Offline dynamic connectivity

The problem: a graph changes by `add edge` and `remove edge` operations; between them come queries "are `u` and `v` connected?". Union-find supports only additions. A removal needs the structure to forget an edge, which is not possible with ordinary union-find.

The offline trick, with all operations known in advance:

1. Each edge is present during an interval of time `[add, remove)`.
2. Build a **segment tree over time**. Store each edge in the `O(log T)` nodes whose intervals together cover its interval exactly.
3. Walk the tree depth-first. On entering a node, `unite` all the edges stored there (remember the snapshot). At a leaf (a time point) the union-find contains exactly the edges alive at that time: every edge alive is stored in exactly one node on the root-to-leaf path. Answer the query at that leaf. On leaving a node, `rollback_to` the snapshot.

```cpp
// Offline dynamic connectivity. Operations arrive in time order: add edge, remove edge, ask "are u and v connected?".
// Every edge is present during a time interval [add, remove). Put each edge into the O(log T) nodes of a segment tree over time
// that cover its interval. A depth-first walk of that tree adds the edges of each node on the way down and rolls them back
// on the way up, so at the leaf for time t exactly the edges alive at t are in the union-find.
struct Op { int type, u, v; };                        // type 0: add edge, 1: remove edge, 2: query
struct DynamicConnectivity {
    int n, T;
    vector<Op> ops;
    vector<vector<pair<int, int>>> seg;
    RollbackDsu dsu;
    vector<char> answer;
    long long unions = 0;                             // successful unions performed: a measure of the work
    DynamicConnectivity(int nn, const vector<Op>& o) : n(nn), T((int)o.size()), ops(o), seg(4 * std::max(1, T)), dsu(nn), answer(o.size(), 0) {}
    void put(int node, int lo, int hi, int l, int r, pair<int, int> e) {   // add e to the nodes covering [l, r) inside [lo, hi)
        if (r <= lo || hi <= l) return;
        if (l <= lo && hi <= r) { seg[node].push_back(e); return; }
        int mid = (lo + hi) / 2;
        put(2 * node, lo, mid, l, r, e);
        put(2 * node + 1, mid, hi, l, r, e);
    }
    void walk(int node, int lo, int hi) {
        size_t s = dsu.snapshot();
        for (const auto& e : seg[node]) if (dsu.unite(e.first, e.second)) ++unions;
        if (hi - lo == 1) { if (ops[lo].type == 2) answer[lo] = dsu.find(ops[lo].u) == dsu.find(ops[lo].v); }
        else { int mid = (lo + hi) / 2; walk(2 * node, lo, mid); walk(2 * node + 1, mid, hi); }
        dsu.rollback_to(s);
    }
    vector<char> run() {                              // answers to the query operations, in order
        std::map<pair<int, int>, int> open;
        for (int t = 0; t < T; ++t) {
            pair<int, int> e{std::min(ops[t].u, ops[t].v), std::max(ops[t].u, ops[t].v)};
            if (ops[t].type == 0) open[e] = t;
            else if (ops[t].type == 1) { put(1, 0, T, open[e], t, e); open.erase(e); }
        }
        for (const auto& kv : open) put(1, 0, T, kv.second, T, kv.first);    // edges never removed live to the end
        if (T > 0) walk(1, 0, T);
        vector<char> res;
        for (int t = 0; t < T; ++t) if (ops[t].type == 2) res.push_back(answer[t]);
        return res;
    }
};
```

```
T4 50000 vertices, 200000 operations (79948 add, 39962 remove, 80090 query): 80090 queries answered, 25109 of them "connected"; 300 queries spread over the whole run (every 267th) agree with rebuilding a union-find from the 19928 live edges (average): 300; edge intervals were stored in 886936 tree nodes (11.1 per added edge), 830018 successful unions in the walk; union-find back to 50000 components at the end: yes
```

50,000 vertices and 200,000 operations: about 80,000 additions, 40,000 removals and 80,000 queries. The edge intervals were stored in 11.1 tree nodes per added edge (the `log T` of the covering), and the walk performed 830,018 successful unions. To check the answers, 300 queries spread over the whole run were compared with rebuilding a union-find from the live edges (about 19,900 of them at that point). The cost of that naive method, and the time for the whole run:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| all 200,000 operations, offline connectivity | 166 / 187 |
| rebuild a union-find from the live edges for **300** queries | 74 / 82 |

A rebuild takes about 0.26 ms, so the 80,090 queries by that method would take about 20 seconds (extrapolated from the 300), against 0.17 to 0.19 s here: roughly 100 times less. The total cost is `O((n + q) log q log n)`. This is the standard solution to "connectivity with deletions" when you may read all the operations first (from memory: online algorithms with polylogarithmic bounds exist, and are far more complicated).

## 7. Undo logs and transactions

A key-value store with transactions: `begin()`, `set`, `erase`, `rollback()` (discard everything since the last `begin()`), `commit()` (keep it). Transactions nest. The direct design copies the whole map at `begin()`: correct and `O(size)` per transaction. The **undo log** design records, before each change, how to reverse it (the key, whether it existed, and its old value), and `rollback()` replays those records backwards. The cost is proportional to the number of changes, not to the size of the map. An inner `commit()` must **keep** its records, so that an outer `rollback()` still undoes them; only the outermost commit may discard the log.

```cpp
// A key-value map with nested transactions, by an UNDO LOG: before each change, record how to reverse it.
// begin() marks the log; rollback() reverses the changes back to the last mark; commit() drops the mark (an inner commit keeps
// its records, so an outer rollback still undoes them). Cost: O(changes made), not O(size of the map).
struct TxMap {
    struct Undo { int key; bool had; int old; };
    std::unordered_map<int, int> m;
    vector<Undo> log;
    vector<size_t> marks;
    void begin() { marks.push_back(log.size()); }
    void set(int k, int v) {
        auto it = m.find(k);
        if (!marks.empty()) log.push_back({k, it != m.end(), it != m.end() ? it->second : 0});
        m[k] = v;
    }
    void erase(int k) {
        auto it = m.find(k);
        if (it == m.end()) return;
        if (!marks.empty()) log.push_back({k, true, it->second});
        m.erase(it);
    }
    bool get(int k, int& v) const { auto it = m.find(k); if (it == m.end()) return false; v = it->second; return true; }
    bool rollback() {
        if (marks.empty()) return false;
        size_t s = marks.back(); marks.pop_back();
        while (log.size() > s) {
            Undo u = log.back(); log.pop_back();
            if (u.had) m[u.key] = u.old; else m.erase(u.key);
        }
        return true;
    }
    bool commit() {
        if (marks.empty()) return false;
        marks.pop_back();
        if (marks.empty()) log.clear();               // the outermost commit makes the changes final
        return true;
    }
};
// The simple alternative: copy the whole map at begin().
struct CopyTxMap {
    std::unordered_map<int, int> m;
    vector<std::unordered_map<int, int>> saved;
    void begin() { saved.push_back(m); }
    void set(int k, int v) { m[k] = v; }
    bool rollback() { if (saved.empty()) return false; m = std::move(saved.back()); saved.pop_back(); return true; }
    bool commit() { if (saved.empty()) return false; saved.pop_back(); return true; }
};
```

A map with 100,000 keys, 200 transactions of 50 writes each, all rolled back:

```
T5 map with 100000 keys, 200 transactions of 50 writes each, all rolled back: undo log and whole-map copy leave the same map: yes; undo log records left 0
```

| Method | Time (ms), run 1 / run 2 |
|---|---|
| undo log, 200 transactions | 0.49 / 0.44 |
| copy the whole map at `begin()`, 200 transactions | 538 / 590 |

The undo log is about 1,000 times faster here (a few microseconds against several milliseconds per transaction), since 50 writes are logged rather than 100,000 entries copied. This is how databases and editors implement undo (from memory, in outline: a write-ahead or undo log) and what an interviewer expects when the question is "design a transactional key-value store".

## 8. Choosing between them

| Need | Choice |
|---|---|
| Read any old state, change only the newest, changes are independent per element | Fat nodes (section 2) |
| Read and change any version, the state is a tree or array | Path copying (sections 3 and 4) |
| Queries about "the structure at time `t`" or "the prefix `i`" over a static input | Persistent segment tree, versions by prefix or time (section 4) |
| Offline, versions visited depth-first, structure is union-find-like | Rollback (sections 5 and 6) |
| Transactions with a stack of nested scopes | Undo log (section 7) |
| Small state, rare snapshots | Copy the state |

A related offline technique (from memory): when queries are on the **nodes of a tree of versions** (each version is a child of another one), walk the version tree depth-first, apply an operation on the way down and undo it on the way up. This needs a rollback structure, not a persistent one, and is usually shorter.

## 9. Complexities

| Operation | Time | Extra memory |
|---|---|---|
| Snapshot array: `set`, `snap` | `O(1)` | `O(1)` per change |
| Snapshot array: `get` | `O(log changes at that index)` | none |
| Persistent array: `set`, `get` | `O(log n)` | `O(log n)` new nodes per update |
| Persistent count tree: insert | `O(log V)` | `O(log V)` per version |
| k-th smallest in a range | `O(log V)` | `O(n log V)` for the build |
| Rollback union-find: `find`, `unite` | `O(log n)` | `O(1)` per union on the stack |
| `rollback_to(s)` | `O(unions undone)` | none |
| Offline dynamic connectivity | `O((n + q) log q log n)` | `O(q log q)` for the tree of intervals |
| Undo-log transaction | `O(changes)` | `O(changes)` |

## 10. Common mistakes

1. **Changing a shared node.** One in-place write corrupts every version that shares the node. Copy first, then change the copy.
2. Taking a reference to a node and then pushing to the vector that stores the nodes: a reallocation invalidates the reference.
3. Path compression in a rollback union-find (it cannot be undone cheaply), or forgetting union by size so the trees are deep.
4. A rollback that restores the parent but not the component size or count: connectivity looks right, and later unions are badly balanced or the component count is wrong.
5. Segment tree over time: a half-open interval turned closed (the edge disappears one step early or late), or edges never removed dropped from the tree.
6. A nested transaction whose inner `commit()` throws away its log records, so the outer `rollback()` leaves changes behind.
7. Logging a new key as "existed before" (rollback then writes a stale value instead of erasing it), or not logging an erase.
8. Snapshot array: using the number of snapshots **so far** as the id of the snapshot being taken (off by one), or returning the new count instead of the id.
9. Persistent structures **without** a plan for freeing old versions: memory grows with every update.
10. Choosing persistence when the versions are visited depth-first (rollback is simpler), or rollback when you need to jump between versions.

## 11. Questions to answer out loud

1. What is a version? What is the difference between partial and full persistence?
2. How does path copying keep old versions valid? How many nodes does one update create, and why?
3. Why does a persistent structure give up some read speed compared with an ordinary array?
4. How does a snapshot array avoid copying? What does `get(i, id)` search for?
5. Describe the k-th smallest in a range with a persistent segment tree. Why is "version `r + 1` minus version `l`" the elements of the subarray?
6. Why can a rollback union-find not use path compression? What bounds the depth instead?
7. What exactly does `rollback_to` restore?
8. Why does a segment tree over time give the edges alive at each time point? Why `log T` nodes per edge?
9. Why is the offline dynamic connectivity cost `O((n + q) log q log n)`?
10. Compare the undo log with copying the map. When is copying the better choice?
11. What must an inner `commit()` do, so an outer `rollback()` still works?
12. When would you pick rollback over persistence?

## 12. Problems

### 12.1 On paper

**A.** A persistent array of 8 elements by path copying. How many nodes does the build create, and how many does each update create? Repeat for 5 elements (not a power of two).

**B.** The array `[5, 2, 8, 2, 9, 1]` and the persistent count tree over values. What are the compressed values? Find the k-th smallest for `(l, r, k)` = (1, 4, 2), (0, 5, 4), (2, 5, 1) and (1, 1, 1), and say which two versions are subtracted for the first.

**C.** A rollback union-find on 6 vertices: `unite(0,1)`, `unite(2,3)`, take a snapshot, then `unite(1,2)`, `unite(4,5)`, `unite(0,3)`. What does each union return, how many components are there before the rollback, and what are the facts "0 and 3 connected", "4 and 5 connected", "0 and 1 connected" before and after `rollback_to(snapshot)`?

**D.** Offline dynamic connectivity on 3 vertices and 8 operations: 0: add 0-1; 1: add 1-2; 2: query 0,2; 3: remove 1-2; 4: query 0,2; 5: add 1-2; 6: query 0,2; 7: query 0,1. Give the answers, the interval of each edge, and the segment tree nodes (over `[0, 8)`) that each interval is stored in.

**E.** A transactional map (undo log), starting empty. Sequence 1: `set(1,10)`, `begin`, `set(1,20)`, `set(2,5)`, `begin`, `erase(1)`, `set(3,7)`, `rollback`, `commit`. Sequence 2: the same but the last two operations are `commit` (inner) then `rollback` (outer). Give the final map in each case.

**F.** Choose the structure for each: (i) 100,000 snapshots of a 100,000 element array with 3 writes between snapshots; (ii) 200,000 queries "k-th smallest in `a[l..r]`", no updates; (iii) 80,000 edge additions and removals with connectivity queries in between, all known in advance; (iv) a key-value store with nested transactions; (v) an editor's undo with a few hundred changes.

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | How to approach |
|---|---|---|
| Snapshot Array | 1146 | Section 2: per-index list of (id, value) and a binary search |
| Time Based Key-Value Store | 981 | The same fat-node idea with timestamps (13.03) |
| Find All People With Secret | 2092 | Group meetings by time; for each group unite, then **roll back** the people who did not learn the secret |
| Checking Existence of Edge Length Limited Paths | 1697 | Offline: sort edges and queries by limit and sweep with a union-find (no rollback needed) |
| Last Day Where You Can Still Cross | 1970 | Run time backwards, adding cells, with a union-find (or binary search with BFS) |
| Design a File System with transactions | custom | Undo log, as in section 7 |

The k-th smallest in a range appears in competitive programming (for example the classic "K-th number" problems on SPOJ and Codeforces, from memory) more than on LeetCode.

### 12.3 In your editor

**A.** Write a persistent stack and a persistent queue (two stacks) and test every old version against a copy. What happens to the amortised bound of the queue when you use an old version repeatedly?

**B.** Extend the persistent array to a persistent **map** (a path-copying binary search tree, or a hash array mapped trie if you know it) and compare memory with copying for 10,000 versions.

**C.** Add parity (bipartite check) to the rollback union-find, and use it with the segment tree over time to answer "is the graph bipartite after each operation?".

**D.** Solve "distinct values on the path from the root to every node" for a tree where each node has a value, by a DFS with an undo log (add a value on entry, undo it on exit), then by a persistent structure, and compare memory.

**E.** Make the transactional map handle savepoints with names (`savepoint("a")`, `rollback_to("a")`), and test against copies.

### Answers

The program that checks the paper problems printed:

```
A n=8: 15 nodes after the build; new nodes for updates at index 0, 7, 4 (each on version 0): 4 4 4 ; total 27
A n=5: 9 nodes after the build; new nodes for updates at index 0, 4, 2 (each on version 0): 3 4 3 ; total 19
B values 5 distinct, 21 nodes; kth(1,4,2) = 2; kth(0,5,4) = 5; kth(2,5,1) = 1; kth(1,1,1) = 2
C unions return 1 1 | snapshot after 2 unions | 1 1 0; then 2 components, 0 and 3 connected: 1, history 4; after the rollback: 4 components, 0 and 3 connected: 0, 4 and 5 connected: 0, 0 and 1 connected: 1, history 2
D answers: yes no yes yes | edge 0-1 on [0,8): [0,8) | edge 1-2 on [1,3): [1,2) [2,3)  and on [5,8): [5,6) [6,8) | successful unions 5
E first sequence: inner rollback 1, outer commit 1, map: 1=20 2=5 , log size 0
E second sequence: inner commit 1, outer rollback 1, map: 1=10
```

**A.** For 8 elements the build creates 15 nodes (8 leaves and 7 inner nodes), and every update creates 4 nodes (the root, two inner nodes and the leaf). For 5 elements the build creates 9 nodes, and an update creates 3 or 4 nodes depending on the depth of the leaf (the tree is not full): the update at index 0 and the one at index 2 created 3 nodes each, at index 4 created 4.

**B.** The distinct values are 1, 2, 5, 8, 9. The answers are 2, 5, 1 and 2. For `(1, 4, 2)` the elements are 2, 8, 2, 9 (sorted 2, 2, 8, 9), so the second smallest is 2, and the versions subtracted are the one holding the first 5 elements (`root[5]`) and the one holding the first 1 (`root[1]`). The tree has 21 nodes: the empty node and 20 created by six insertions.

**C.** The unions return true, true, then true, true, false (0 and 3 were already connected through 1 and 2). Before the rollback there are 2 components, 0 and 3 are connected, 4 and 5 are connected, and 0 and 1 are connected. After `rollback_to` the snapshot (taken after 2 unions) the history has 2 records, there are 4 components, 0 and 3 are **not** connected, 4 and 5 are **not** connected, and 0 and 1 **are** connected (that union was before the snapshot).

**D.** The answers are yes, no, yes, yes. Edge 0-1 is alive during `[0, 8)`, stored in the root. Edge 1-2 is alive during `[1, 3)` (added at 1, removed at 3) and `[5, 8)`. The first interval is stored in the nodes `[1,2)` and `[2,3)`, the second in `[5,6)` and `[6,8)`. At time 4 (the query between the removal and the re-addition) neither of those nodes is on the path to the leaf, so only the edge 0-1 is present, which is why the second answer is no.

**E.** Sequence 1: the inner rollback undoes `erase(1)` and `set(3,7)`, the outer commit keeps the rest: the map is `{1: 20, 2: 5}` and the log is empty. Sequence 2: the inner commit keeps its records, then the outer rollback undoes everything since the outer `begin`, including the inner changes: the map is `{1: 10}`.

**F.** (i) Fat nodes (a snapshot array). (ii) A persistent segment tree over values, one version per prefix. (iii) Offline dynamic connectivity: a segment tree over time and a union-find with rollback. (iv) An undo log, with a mark per `begin`. (v) An undo log (or the command pattern with an inverse per command), since only a few hundred changes need to be kept.

## Cheat sheet

- **Persistent:** every version stays readable (and, if full, writable). **Rollback:** only the present, undo in reverse order.
- **Fat nodes:** per element, list of (version, value); `get` is a binary search. 399,993 entries for 300,000 writes against 40 GB of full copies.
- **Path copying:** copy the nodes on the root-to-leaf path (17 per update at `n = 65,536`), share the rest; 100,001 versions in 21 MB against 26 GB; never change a shared node.
- **K-th smallest in a range:** persistent count tree over compressed values, version per prefix, walk `root[r+1]` and `root[l]` together; about 1 microsecond per query against 0.43 ms by copying and selecting.
- **Rollback union-find:** no path compression, union by size (depth at most `log2 n`; measured 7 at 100,000 vertices), history stack of (child root, parent root); `rollback_to` restores parent, size and component count.
- **Offline dynamic connectivity:** each edge's interval goes to `O(log T)` nodes of a segment tree over time; walk depth-first with `unite` on entry and `rollback_to` on exit; 80,090 queries in under 0.2 s against about 20 s by rebuilding.
- **Undo log:** record how to reverse each change; inner commit keeps its records; about 1,000 times faster than copying the map for small transactions.
- **Say in an interview:** name the access pattern (random versions or depth-first), the cost per update, and the memory per version.

---

## Sources

- All code is tested here (38,391,286 checks under ASan and UBSan; 21 deliberate bugs all caught, one only after adding a size check, three by crashing). The traces and timings are measured on one Xeon container, two runs each, with identical traces. The data is random and seeded. Memory figures are node counts times 12 bytes, not allocator measurements.
- The techniques (fat nodes, path copying, a persistent segment tree for range order statistics, union-find with rollback, the segment tree over time for offline dynamic connectivity, undo logs for transactions) are standard; I describe them from memory and tested them here. The classification of persistence (partial, full, confluent), the remark that functional languages make persistence the default, the existence of online polylogarithmic dynamic connectivity, the use of undo logs in databases and editors, the SPOJ and Codeforces examples and the LeetCode numbers are from memory and were not checked.
- The explanations, traces and code are my own.
