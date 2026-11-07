# DSA 07.17: Tries: Prefix Trees, Child Storage, Autocomplete, Word Search and What They Cost

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (4 reading, 5 problems) · You need: [Hash tables inside](../02_hashing/01_hash_tables_inside.md) (the baseline a trie has to beat), [Binary search templates](../03_binary_search_and_sorting/01_binary_search_templates.md) (the sorted-array alternative), and [Inside std::set and std::map](09_inside_std_set_and_map.md) (the ordered-tree alternative). There is no companion demo file: the trie is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- write a trie with **insert, search, prefix search and delete** (including pruning the dead branch), keeping nodes in one vector and children in whichever of **three layouts** fits the alphabet;
- compute the **memory per word** of each layout from `sizeof` and the allocator's rules, and explain why a 26-slot array costs 318 bytes per word where `std::set<std::string>` costs 80;
- answer **how many words start with p** in `O(|p|)` and list the first few in sorted order, and say honestly which of these a trie wins and which a sorted array wins;
- solve **autocomplete, wildcard search, longest common prefix, Word Search II and Replace Words** with the same structure;
- read the shape of a trie (how many nodes have one child) and see why that leads to **path compression**, the subject of Topic 18.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 946,157 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran each child layout on its own against a `std::map` (1,400 random runs), and then random inserts and erases against a `std::set<std::string>` for all four layouts (26 letters, 256 byte values, sorted vector and map; 600 random runs of 60 operations each, with small alphabets so that words collide and share prefixes); after every operation the trie was checked against the model for membership, prefix test, prefix count, the first words of an autocomplete, the longest common prefix, and its node count against the number of distinct prefixes of the model. Further tests: wildcard search against a brute-force scan; `shape_of` against an independently computed prefix set (200 tries); Word Search II against a per-word depth-first search on 400 random boards; Replace Words against a brute-force scan on 300 sentences; a trie 200,000 letters deep (to prove nothing recurses); and the reuse of freed nodes (the node pool does not grow when the same 1,000 words are erased and inserted again). The invariant checker `valid()` was attacked through a test hook (the `friend struct TestAccess` line): 19 deliberate corruptions (a wrong pass count, a lost or invented word end, a node on the free list that is still reachable, a leaked node, a cycle, a dangling link, and six that the final node count alone would not notice: the root on the free list, an id equal to the vector size, a free id listed twice, a subtree with two parents, an empty branch) must all be reported. Finally 89 hand-made mutants of the code were run against the tests (wrong index formulas, a missing prune, a lost counter, a reference held across a reallocation and so on): **81 were caught and 8 are equivalent changes** that cannot alter any output (listed after the common mistakes).

**Two data sets, and how the timings were taken.** The "headers" set is the 23,924 distinct lowercase words of three to twenty letters found in the files under `/usr/include` on the test machine (identifiers and comments, 172,095 letters; an engineering vocabulary, not English prose). The "random" set is 200,000 distinct random strings of 6 to 12 letters, seed 9, which share almost nothing beyond their first letters, the worst case for a trie. Times are nanoseconds per word or per query on a 2-core Intel Xeon VM at 2.10 GHz (Ubuntu 24.04, GCC 13.3, `-O2`), the best of 7 repetitions, in a loop over a shuffled copy of the words. The machine is noisy, so every timing below is shown as **run 1 / run 2** (two complete runs of the program) and only conclusions that hold in both runs are drawn. Memory is measured with glibc's `mallinfo2` around an **exact-size copy** of the structure, so that the growth slack of `std::vector` does not count. The remarks about the Linux kernel in section 7 and the references are from memory and say so.

---

## 1. What a trie is

A trie (pronounced "try", from re*trie*val) stores a set of strings as paths in a tree. Each edge is labelled with one letter; the root is the empty string; the node reached by spelling `p` stands for the prefix `p`. A node is marked when a stored word ends there. The words `to tea ted ten a inn` give:

```
(root, 6 words through it)
  a   (1, word end)
  i   (1)
    n (1)
      n (1, word end)
  t   (4)
    e (3)
      a (1, word end)
      d (1, word end)
      n (1, word end)
    o (1, word end)
```

(The number in brackets is the count of words that pass through the node; the program's own print of the same trie is in section 3.) Four facts carry everything else in this tutorial.

1. **One node per distinct prefix.** The trie above has 11 nodes: the root, and one for each of `t to te tea ted ten a i in inn`. The node count is therefore the number of distinct prefixes of the stored words plus the root, at most the total number of letters plus one, and smaller the more the words share.
2. **Every operation costs `O(|key|)`, whatever the number of words.** Looking up `ten` is three steps whether the trie holds six words or six million. There is no comparison between keys, no hash, no rebalancing. (A hash table also needs `O(|key|)` to hash the key; a balanced tree needs `O(|key| log n)`, because each of its `log n` comparisons may read up to `|key|` letters.)
3. **Order comes free.** Visiting the children of every node in letter order visits the words in sorted order, so "all words starting with `te`" is one subtree, found by walking `te` and then collecting.
4. **A node knows its subtree.** Anything you can sum over a subtree (here: the number of words, `pass`) is available at the node for `O(1)`, so "how many words start with `te`" is `|p|` steps and a read. This is what hash tables cannot do at all, and what a sorted array can do only with two binary searches.

| Operation | Hash set | Sorted array | `std::set` | Trie |
|---|---|---|---|---|
| exact lookup | `O(L)` expected | `O(L log n)` | `O(L log n)` | `O(L)` |
| insert | `O(L)` expected | `O(n)` shifting | `O(L log n)` | `O(L)` |
| words with prefix p, counted | scan everything | two binary searches | two searches plus a walk | `O(|p|)` with a counter |
| words with prefix p, listed in order | scan and sort | binary search, then a contiguous run | `lower_bound`, then iterate | walk `p`, then the subtree |
| longest stored prefix of a string (Topic 18) | try every prefix | hard | hard | one walk |
| memory per word | small | smallest | small | **large** (section 6) |

The last row is the price, and most of this tutorial is about it.

---

## 2. Child storage: three layouts

What is a node? A word-end flag, a counter, and **a map from a letter to a child**. How that map is stored decides the memory, the speed and which alphabets are practical. The class in section 3 takes the map as a template parameter, so that one algorithm runs on all three layouts and the measurements compare like with like.

```cpp
// Three ways to store the children of ONE node. Each maps a byte to a child id, or -1 for "none",
//    and visits its children in increasing byte order (that order is what makes autocomplete sorted).
template <int Base, int N>
struct ArrayKids {                                   // one slot per letter: 4 * N bytes per node
    int a[N];
    ArrayKids() { for (int& x : a) x = -1; }
    static bool accepts(unsigned char c) { return c >= Base && c < Base + N; }
    int get(unsigned char c) const { return accepts(c) ? a[c - Base] : -1; }
    void set(unsigned char c, int id) { a[c - Base] = id; }
    void erase(unsigned char c) { a[c - Base] = -1; }
    template <class F> void each(F f) const { for (int i = 0; i < N; ++i) if (a[i] >= 0) f((unsigned char)(Base + i), a[i]); }
};

struct SortedKids {                                  // only the children that exist, sorted by byte
    std::vector<std::pair<unsigned char, int>> v;
    static bool accepts(unsigned char) { return true; }
    std::size_t lower(unsigned char c) const {
        return std::lower_bound(v.begin(), v.end(), c, [](const std::pair<unsigned char, int>& p, unsigned char k) { return p.first < k; }) - v.begin();
    }
    int get(unsigned char c) const { std::size_t i = lower(c); return i < v.size() && v[i].first == c ? v[i].second : -1; }
    void set(unsigned char c, int id) {
        std::size_t i = lower(c);
        if (i < v.size() && v[i].first == c) v[i].second = id; else v.insert(v.begin() + i, {c, id});
    }
    void erase(unsigned char c) { std::size_t i = lower(c); if (i < v.size() && v[i].first == c) v.erase(v.begin() + i); }
    template <class F> void each(F f) const { for (const auto& p : v) f(p.first, p.second); }
};

struct MapKids {                                     // a red-black tree per node
    std::map<unsigned char, int> m;
    static bool accepts(unsigned char) { return true; }
    int get(unsigned char c) const { auto it = m.find(c); return it == m.end() ? -1 : it->second; }
    void set(unsigned char c, int id) { m[c] = id; }
    void erase(unsigned char c) { m.erase(c); }
    template <class F> void each(F f) const { for (const auto& p : m) f(p.first, p.second); }
};
```

Each layout offers the same five things (`accepts`, `get`, `set`, `erase`, `each`) and visits children in increasing byte order. The nodes themselves are stored in one `std::vector` and refer to each other by **4-byte indices**, not pointers: no allocation per node, links half the size, and a trie that can be copied, moved or written to a file as it is.

| Layout | `sizeof` (children part / whole node) | Heap memory beyond the node | Child lookup |
|---|---|---|---|
| `ArrayKids<'a', 26>` | 104 / **112** bytes | none | one index, no search |
| `ArrayKids<0, 256>` | 1,024 / **1,032** bytes | none | one index, no search |
| `SortedKids` | 24 / **32** bytes | one block of `8 * children` bytes, at least 32 with glibc | binary search in the vector |
| `MapKids` | 48 / **56** bytes | 48 bytes per child (a 40-byte tree node rounded up by `malloc`) | `O(log children)`, pointer chasing |

(`sizeof` is the program's output; the node is the children plus a 4-byte counter and a flag, padded.) Which to use depends on how full a node is. In the "headers" data **61% of the nodes have exactly one child and 27% have none**; only 5% have three or more (section 6). A 26-slot array holds on average one child in 26 slots, so about 96% of its bytes hold `-1`. With a 256-letter alphabet the node costs 1,032 bytes, and the same words would take about **2,934 bytes per word** (arithmetic: 68,013 nodes times 1,032 bytes over 23,924 words; not measured). The rule of thumb:

- **Small dense alphabet** (DNA, digits, lowercase letters) and speed matters: the array.
- **Large alphabet or sparse nodes** (bytes, Unicode code points, words as symbols): the sorted vector, which costs half the array's memory here and a binary search on a handful of entries. Its insertion cost is `O(children)` for the shift, which is tiny for bytes.
- **A map** is the layout to try last: it is the most flexible and, in these measurements, never the fastest and not much smaller than the array.

Two other layouts exist and are not implemented here. **First-child / next-sibling** keeps a link to the first child and each node a link to its next sibling (about 12 bytes of links and letter per node, by arithmetic), but a lookup walks a linked list of up to 26 (or 256) entries. **A hash table keyed by (node, letter)** stores all the edges of the trie in one table. A **bitmap plus a packed array** (one bit per letter says which children exist; the children are stored contiguously and found by counting set bits) gives array-like lookup at vector-like memory; it is the idea behind the hash array mapped tries of functional languages (from memory). None of these was measured here.

---

## 3. The trie

```cpp
// A set of strings. Nodes live in one vector and refer to each other by index (a pointer-free layout:
//    no allocation per node, and 4-byte links). `pass` counts the words that go through a node.
template <class Kids>
class Trie {
    struct Node { Kids kids; int pass = 0; bool end = false; };
    std::vector<Node> nodes_;
    std::vector<int> free_;                          // ids of deleted nodes, reused first
    friend struct TestAccess;

    int make() {
        if (!free_.empty()) { int id = free_.back(); free_.pop_back(); nodes_[id] = Node{}; return id; }
        nodes_.emplace_back();
        return (int)nodes_.size() - 1;
    }

public:
    Trie() { nodes_.emplace_back(); }                // node 0 is the root and is never freed

    int root() const { return 0; }
    std::size_t size() const { return nodes_[0].pass; }                 // words stored
    std::size_t node_count() const { return nodes_.size() - free_.size(); }

    int find(std::string_view s) const {             // the node for the prefix s, or -1
        int v = 0;
        for (unsigned char c : s) { v = nodes_[v].kids.get(c); if (v < 0) return -1; }
        return v;
    }
    bool contains(std::string_view s) const { int v = find(s); return v >= 0 && nodes_[v].end; }
    bool starts_with(std::string_view p) const { int v = find(p); return v >= 0 && nodes_[v].pass > 0; }
    std::size_t count_prefix(std::string_view p) const { int v = find(p); return v < 0 ? 0 : nodes_[v].pass; }

    bool insert(std::string_view s) {
        if (contains(s)) return false;
        for (unsigned char c : s) if (!Kids::accepts(c)) throw std::out_of_range("trie: byte outside the alphabet");
        int v = 0;
        ++nodes_[0].pass;
        for (unsigned char c : s) {
            int nx = nodes_[v].kids.get(c);
            if (nx < 0) { nx = make(); nodes_[v].kids.set(c, nx); }     // make() may move nodes_: no reference is held across it
            v = nx;
            ++nodes_[v].pass;
        }
        nodes_[v].end = true;
        return true;
    }

    bool erase(std::string_view s) {
        if (!contains(s)) return false;
        std::vector<int> path{0};
        for (unsigned char c : s) path.push_back(nodes_[path.back()].kids.get(c));
        for (int v : path) --nodes_[v].pass;
        nodes_[path.back()].end = false;
        for (std::size_t i = 1; i < path.size(); ++i)
            if (nodes_[path[i]].pass == 0) {         // from here down the path carried only this word: cut it off
                nodes_[path[i - 1]].kids.erase((unsigned char)s[i - 1]);
                for (std::size_t j = i; j < path.size(); ++j) free_.push_back(path[j]);
                break;
            }
        return true;
    }

    // read-only view of the nodes, for the algorithms below
    int child(int v, unsigned char c) const { return nodes_[v].kids.get(c); }
    bool is_end(int v) const { return nodes_[v].end; }
    int pass(int v) const { return nodes_[v].pass; }
    template <class F> void each_child(int v, F f) const { nodes_[v].kids.each(f); }

    bool valid() const {                             // the invariants, checked by walking the whole trie
        std::vector<char> seen(nodes_.size(), 0);
        for (int f : free_) { if (f <= 0 || f >= (int)nodes_.size() || seen[f]) return false; seen[f] = 2; }
        std::vector<int> st{0};
        seen[0] = 1;
        std::size_t reach = 0;
        while (!st.empty()) {
            int v = st.back(); st.pop_back();
            ++reach;
            long sum = nodes_[v].end ? 1 : 0;
            bool ok = true;
            nodes_[v].kids.each([&](unsigned char, int ch) {
                if (ch <= 0 || ch >= (int)nodes_.size() || seen[ch]) { ok = false; return; }
                seen[ch] = 1; sum += nodes_[ch].pass; st.push_back(ch);
            });
            if (!ok || sum != nodes_[v].pass || (v != 0 && nodes_[v].pass <= 0)) return false;
        }
        return reach + free_.size() == nodes_.size();
    }
};
```

Reading it in the order that matters:

**`insert`.** First `contains`: a word already present changes nothing. Then one walk from the root that creates the missing nodes and, on the way, adds one to `pass` of every node on the path, the root included. The classic bug is in the middle of the loop: `make()` appends to the vector and may **move all the nodes to a new block**, so any reference or pointer taken into `nodes_` before the call dangles afterwards. The code therefore calls `make()`, stores the id in a local, and only then indexes the vector again. (One of the mutants writes `nodes_[v].kids.set(c, make())` in a single expression: in C++17 the object `nodes_[v]` is evaluated first, `make()` then moves the vector, and the write lands in freed memory. The tests catch it, because the sanitizer reports the write.)

**`pass` is the whole trick.** It is the number of stored words whose path goes through the node. It gives `count_prefix` for free, and it makes deletion simple: after a word is removed, the first node on its path whose `pass` fell to 0 carried **only that word**, so the node and everything below it (a plain chain, nothing else passes there) can be cut off and freed in one go. Without the counter, deleting needs a bottom-up pass that asks every node "do you still have other children or an end flag?".

**`erase`** therefore: check the word exists; collect the path; subtract one from `pass` along the path; clear the end flag; find the first node whose `pass` is now 0; unlink it from its parent and put it and its descendants on the free list. Ids from the free list are reused by `make()` before the vector grows.

**`valid()`** is the checker used by the tests: every reachable node is visited once, `pass` equals the end flag plus the sum of the children's `pass`, no non-root node has `pass` 0, the free list holds only unreachable nodes once each, and reachable plus free nodes account for the whole vector (no leak).

**What the program prints** for the six words above (a node is `(pass children...)`, a star marks a word end; this is the program's own output):

```
T1 node_count after each insert of to tea ted ten a inn: 3 5 6 7 8 11
T2 (6 a(1*) i(1 n(1 n(1*))) t(4 e(3 a(1*) d(1*) n(1*)) o(1*)))
T5 erase ted: nodes 10; (5 a(1*) i(1 n(1 n(1*))) t(3 e(2 a(1*) n(1*)) o(1*)))
T5 erase inn: nodes 7; (4 a(1*) t(3 e(2 a(1*) n(1*)) o(1*)))
T5 erase to: nodes 6; (3 a(1*) t(2 e(2 a(1*) n(1*))))
T6 ab abc then erase ab: nodes 4; (1 a(1 b(1 c(1*)))); contains ab 0 abc 1
```

Inserting `to` creates two nodes (`t`, `to`), `tea` two more (`te`, `tea`), and `inn` three, which is where 11 comes from. Erasing `ted` removes just its leaf; erasing `inn` removes the whole branch `i n n`, because `i` had only that word through it; erasing `to` removes the leaf `o` but leaves `t`, which `tea` and `ten` still use. The last line is the case people get wrong: **erasing a word that is a prefix of another** only clears the end flag, and all four nodes stay.

**Bytes outside the alphabet.** `contains`, `find` and the prefix operations answer "no" for a byte the layout cannot hold (`get` returns -1). `insert` throws `std::out_of_range` **before changing anything**, so the trie is never left half-built. `ArrayKids<0, 256>` accepts every byte, including `'\0'`, as long as the key is a `string_view` and not a C string.

---

## 4. Prefix operations

**Counting.** `count_prefix("te")` is `find` plus one read of `pass`: here 3 (`tea ted ten`); `count_prefix("t")` is 4, `count_prefix("")` is 6 and `count_prefix("tex")` is 0. The empty prefix is every word, and `starts_with("")` is true exactly when the set is not empty.

**Autocomplete** lists the words under a prefix in sorted order and stops after `limit`, so asking for ten suggestions never walks the whole subtree.

```cpp
// Autocomplete: up to `limit` stored words that start with p, in sorted order.
//    An iterative depth-first walk; each frame remembers how long the string was above it.
template <class T>
std::vector<std::string> complete(const T& t, std::string_view p, std::size_t limit) {
    std::vector<std::string> out;
    int start = t.find(p);
    if (start < 0 || limit == 0) return out;
    struct Frame { int node; std::size_t len; int c; };      // c < 0: the start node, nothing to append
    std::vector<Frame> st{{start, p.size(), -1}};
    std::string cur(p);
    while (!st.empty() && out.size() < limit) {
        Frame f = st.back(); st.pop_back();
        cur.resize(f.len);                                   // go back up to the parent's string
        if (f.c >= 0) cur.push_back((char)f.c);
        if (t.is_end(f.node)) out.push_back(cur);
        std::size_t mark = st.size();
        t.each_child(f.node, [&](unsigned char c, int ch) { st.push_back({ch, cur.size(), c}); });
        std::reverse(st.begin() + mark, st.end());           // so the smallest byte is popped first
    }
    return out;
}
```

It is an explicit stack, so a very deep trie cannot overflow the call stack (the tests build one 200,000 letters deep). The trick is in the frame: it stores the **length** of the string above it, and the loop shortens the one shared `cur` string to that length before appending the frame's letter, so no string is copied per node. The children are pushed in reverse so that the smallest letter is popped first. For the trie above the program prints `complete te: tea ted ten`, `complete t limit 2: tea ted` (sorted order puts `tea` before `to` because `e < o`) and `complete empty: a inn tea ted ten to`.

**Wildcard search** ("Design Add and Search Words"): a pattern with `.` standing for any one letter. At a letter follow one child; at a dot fan out into all of them.

```cpp
// Does some stored word match the pattern, where '.' stands for any one byte?
//    Depth-first over (node, position in the pattern); a '.' fans out into every child.
template <class T>
bool match_dots(const T& t, std::string_view pat) {
    std::vector<std::pair<int, std::size_t>> st{{t.root(), 0}};
    while (!st.empty()) {
        int v = st.back().first;
        std::size_t i = st.back().second;
        st.pop_back();
        if (i == pat.size()) { if (t.is_end(v)) return true; continue; }
        if (pat[i] == '.') t.each_child(v, [&](unsigned char, int ch) { st.push_back({ch, i + 1}); });
        else { int ch = t.child(v, (unsigned char)pat[i]); if (ch >= 0) st.push_back({ch, i + 1}); }
    }
    return false;
}
```

In the worst case a pattern of dots visits every node of the right depth, so the cost is not `O(|pattern|)` any more; it is bounded by the number of nodes in the trie. For `bad dad mad` the program prints `pad 0 bad 1 .ad 1 b.. 1 b. 0`: `b.` is false because every word has three letters, a length check the walk makes for free by requiring the word end exactly at the end of the pattern.

**Longest common prefix of all the words.** Follow the root while the node has exactly one child and is not a word end (a word end means some word stops here, so nothing longer is shared by all).

```cpp
// The longest prefix shared by every stored word: follow the root while the node
//    has exactly one child and is not itself the end of a word.
template <class T>
std::string longest_common_prefix(const T& t) {
    std::string out;
    int v = t.root();
    while (!t.is_end(v)) {
        int kids = 0, only = -1;
        unsigned char oc = 0;
        t.each_child(v, [&](unsigned char c, int ch) { ++kids; only = ch; oc = c; });
        if (kids != 1) break;
        out.push_back((char)oc);
        v = only;
    }
    return out;
}
```

The program prints `'fl'` for `flower flow flight` (the walk stops at `fl`, which has two children, `o` and `i`) and `'flow'` for `flower flow` (the walk stops at `flow`, which has one child but is itself a word, so nothing longer is shared by all).

---

## 5. Two classic problems

**Word Search II.** Given a board of letters and a list of words, report every word that can be traced through side-adjacent cells, each cell used once per word. The naive method runs a depth-first search per word. The trie turns the list into one structure, so a single walk over the board follows only the paths that are a prefix of some word, and abandons a path the moment no word continues it.

```cpp
// Word Search II: which of `words` (lowercase) can be traced on the board through side-adjacent
//    cells, using each cell at most once per word?  The trie lets one walk of the board serve all words.
inline std::vector<std::string> word_search(std::vector<std::string> board, const std::vector<std::string>& words) {
    Trie<ArrayKids<'a', 26>> t;
    for (const auto& w : words) t.insert(w);
    std::vector<std::string> found;
    std::string cur;
    const int R = (int)board.size(), C = R ? (int)board[0].size() : 0;
    std::function<void(int, int, int)> dfs = [&](int r, int c, int node) {
        char ch = board[r][c];
        int nx = t.child(node, (unsigned char)ch);           // '#' (a used cell) is outside a..z: answers -1
        if (nx < 0) return;
        cur.push_back(ch);
        if (t.is_end(nx)) { found.push_back(cur); t.erase(cur); }   // report a word once, then forget it
        if (t.find(cur) >= 0) {                              // erase() already cut a branch with nothing left below
            board[r][c] = '#';
            static const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int a = r + dr[k], b = c + dc[k];
                if (a >= 0 && a < R && b >= 0 && b < C) dfs(a, b, nx);
            }
            board[r][c] = ch;
        }
        cur.pop_back();
    };
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) dfs(r, c, t.root());
    return found;
}
```

Three details decide whether this passes a time limit. The `child()` call answers -1 for `'#'`, the letter that marks a cell in use, because `'#'` is outside `a..z`; no separate visited array is needed. When a word is found it is **erased from the trie**: it is reported once and the branch that led only to it disappears (that is what the pruning in `erase` is for), so later walks that reach the same cells stop earlier. And the check `t.find(cur) >= 0` afterwards stops the recursion when `erase` has just cut the branch off. The recursion depth is the length of the longest word, not the size of the board or the trie. On the standard example (a 4 by 4 board, words `oath pea eat rain`) the program prints `oath eat`; `pea` needs a `p`, which is not on the board, and `rain` needs an `a` next to the `r`, which it does not have.

**Replace Words.** Replace each word of a sentence by the shortest dictionary root that is a prefix of it. Walk the word down the trie of roots; the first word end met on the way is the shortest root, because the walk meets shorter prefixes first.

```cpp
// Replace Words: each word of a sentence (single spaces) becomes its shortest dictionary root, if it has one.
inline std::string replace_words(const std::vector<std::string>& roots, std::string_view sentence) {
    Trie<SortedKids> t;
    for (const auto& r : roots) t.insert(r);                 // roots are non-empty
    std::string out;
    std::size_t i = 0;
    while (i < sentence.size()) {
        std::size_t j = sentence.find(' ', i);
        if (j == std::string_view::npos) j = sentence.size();
        std::string_view w = sentence.substr(i, j - i);
        std::size_t keep = w.size();
        int v = t.root();
        for (std::size_t k = 0; k < w.size(); ++k) {
            v = t.child(v, (unsigned char)w[k]);
            if (v < 0) break;
            if (t.is_end(v)) { keep = k + 1; break; }        // the first word end on the way is the shortest root
        }
        if (i > 0) out.push_back(' ');
        out.append(w.substr(0, keep));
        i = j + 1;
    }
    return out;
}
```

With roots `cat bat rat` the sentence "the cattle was rattled by the battery" becomes "the cat was rat by the bat" (program output). Note what the trie buys over checking every root against every word: each word costs at most the length of the longest root, however many roots there are. The roots use the sorted-vector layout because the alphabet is not promised to be lowercase.

---

## 6. What it costs: measurements

First the **shape**. This is the program's own report for the two data sets (`by children` lists how many nodes have 0, 1, 2, ... children).

```cpp
// How branchy is the trie?  by_degree[d] = nodes with d children.  `chain` counts the non-root
//    nodes that are not word ends and have exactly one child: the nodes path compression removes.
struct Shape { std::vector<long> by_degree; long chain = 0; };
template <class T>
Shape shape_of(const T& t) {
    Shape s;
    std::vector<int> st{t.root()};
    while (!st.empty()) {
        int v = st.back(); st.pop_back();
        std::size_t deg = 0;
        t.each_child(v, [&](unsigned char, int ch) { ++deg; st.push_back(ch); });
        if (s.by_degree.size() <= deg) s.by_degree.resize(deg + 1, 0);
        ++s.by_degree[deg];
        if (deg == 1 && v != t.root() && !t.is_end(v)) ++s.chain;
    }
    return s;
}
```

| | headers | random |
|---|---|---|
| words | 23,924 | 200,000 |
| letters in all the words | 172,095 | 1,799,908 |
| trie nodes | 68,013 (2.84 per word) | 1,178,634 (5.89 per word) |
| nodes with 0 children (leaves) | 18,462 (27%) | 199,991 (17%) |
| nodes with exactly 1 child | 41,688 (61%) | 927,109 (79%) |
| nodes with 2 or more children | 7,863 (12%) | 51,534 (4%) |
| `chain` (one child, not a word end, not the root) | 38,859 | 927,100 |
| nodes after path compression (`nodes - chain`) | 29,154 (43%) | 251,534 (21%) |

The headers set shares a lot: 172,095 letters become 68,013 nodes, 2.5 letters per node. The random set shares little beyond the first three levels (1.5 letters per node), yet even there **four nodes in five have a single child**, because after the first few letters a prefix identifies its word, and the rest of the word is a chain. Those chain nodes are the ones a **radix tree** (Patricia tree) merges into a single edge labelled with a string. Counting them is the preview for Topic 18: a compressed trie would have 43% and 21% of the nodes above. (The count is of nodes only; the edges then need string labels, so the memory saving is smaller than the node saving.)

**Memory**, in bytes per word, measured on an exact-size copy (the two runs agreed to within 0.1 bytes per word):

| Structure | headers | random |
|---|---|---|
| trie, 26-slot array | **318** | **660** |
| trie, sorted-vector children | 160 | 351 |
| trie, map children | 296 | 613 |
| `std::unordered_set<std::string>` | 78 | 78 |
| `std::set<std::string>` | 80 | 80 |
| sorted `std::vector<std::string>` | 32 | 32 |

The three trie numbers can be reproduced from the table in section 2 without running anything. The array trie is 68,013 nodes times 112 bytes over 23,924 words: **318.4**. The map trie is 68,013 nodes times 56 bytes plus 68,012 edges times 48 bytes: **295.7**. The sorted-vector trie is 68,013 nodes times 32 bytes plus one malloc block of at least 32 bytes for every node that has children: **160.1**. (The strings in the baselines are at most 20 letters; those of 15 letters or fewer sit inside the 32-byte `std::string` object, which is why the sorted vector costs exactly 32 bytes per word here.) The trie pays 2 to 8 times the memory of `std::set<std::string>` and 5 to 21 times that of the sorted vector, and the "random" row shows the reason: a trie spends a node per letter wherever words stop sharing.

**Time**, ns per word, run 1 / run 2. Build is `insert` of every word; hit is `contains` of every word in shuffled order; miss is `contains` of words with their last letter changed to one that is absent from the set.

| headers (23,924 words) | build | hit | miss |
|---|---|---|---|
| trie, array | 998.0 / 869.2 | 411.3 / 280.4 | 265.4 / 246.7 |
| trie, sorted kids | 664.7 / 650.6 | 544.4 / 651.9 | 515.6 / 448.7 |
| trie, map kids | 1,129.0 / 996.4 | 728.7 / 722.1 | 837.1 / 1,082.4 |
| `unordered_set` | 189.4 / 171.0 | 84.9 / 59.6 | 71.2 / 56.0 |
| `std::set` | 491.8 / 405.8 | 287.9 / 279.1 | 292.2 / 268.2 |
| sorted vector, binary search | | 217.4 / 207.0 | |

| random (200,000 words) | build | hit | miss |
|---|---|---|---|
| trie, array | 1,358.9 / 1,243.9 | 464.3 / 468.6 | 427.6 / 442.8 |
| trie, sorted kids | 1,404.3 / 1,435.7 | 1,087.0 / 1,022.2 | 1,133.0 / 1,164.4 |
| trie, map kids | 2,829.5 / 2,977.5 | 1,665.6 / 1,511.6 | 1,845.3 / 1,908.1 |
| `unordered_set` | 568.1 / 745.6 | 308.9 / 296.0 | 233.6 / 226.2 |
| `std::set` | 1,112.5 / 1,181.0 | 984.8 / 1,025.4 | 857.9 / 944.7 |
| sorted vector, binary search | | 331.6 / 358.5 | |

And the two prefix queries, per query, for a three-letter prefix (the sorted vector counts with two `lower_bound` calls, the trie with `count_prefix`; the counts agreed on every query, 900,020 in all on the headers and 2,477,566 on the random set; "top 10" means ten suggestions at most):

| Query | headers | random |
|---|---|---|
| count words with the prefix: trie | 15.3 / 9.4 ns | 20.2 / 20.4 ns |
| count words with the prefix: sorted vector | 377.3 / 389.7 ns | 587.3 / 605.3 ns |
| top 10 suggestions: trie (`complete`) | 1,982.9 / 1,802.1 ns | 6,244.2 / 7,158.8 ns |
| top 10 suggestions: sorted vector | 241.9 / 231.6 ns | 385.4 / 404.5 ns |

What this says, keeping to what holds in both runs:

1. **Counting by prefix is the trie's win: 25 to 41 times faster** than two binary searches, and the gap does not depend on the size of the set because it is `O(|p|)`.
2. **Autocomplete is the trie's loss in this implementation: the sorted vector is 8 to 18 times faster.** The walk itself is short (21.8 nodes popped per query on the headers, 56.6 on the random set, about 2.9 and 5.9 per returned word), so the time is not in the walk. The sorted vector reads ten neighbouring strings from one place in memory; my `complete` allocates a stack, a result vector and a string per word. I did not profile which of these dominates, and a tuned version (reused buffers, an output iterator) would be faster. The lesson stands: **a trie does not beat a sorted array for "list the matches"; it beats it when the set changes** (the array pays `O(n)` per insert) **or when each node carries a precomputed answer** (the best score below it, a top-10 list).
3. **For exact membership, a hash set wins**: 1.5 to 4.8 times faster than the array trie on a hit, with a quarter or less of the memory. Do not use a trie for plain "is this word in the dictionary".
4. **The layouts rank the same way on lookups**: array fastest, sorted vector 1.3 to 2.3 times slower on a hit, map the slowest (3.2 to 3.6 times slower than the array on the random set). The map is smaller than the array only by a little (613 against 660 bytes per word) and loses on time in every row. On *building* the small set the sorted-vector layout was the fastest (665 / 651 against 998 / 869 for the array), probably because its 32-byte nodes touch fewer cache lines than 112-byte ones; I did not test that.
5. **Against `std::set<std::string>`**: on the small set the array trie and the set are within the noise of each other on a hit (411 against 288 in run 1, 280 against 279 in run 2); on 200,000 words the array trie is 2.1 to 2.2 times faster, because the set's comparison chain (17 or 18 levels of up to 12 letters) costs more than 6 to 12 node visits.

The honest summary: **a trie is not a faster, smaller dictionary.** It is a structure for questions about prefixes, and its cost is memory.

---

## 7. Choosing

| Need | Use |
|---|---|
| is this word in the set | `std::unordered_set` |
| sorted iteration, range of words between two bounds | `std::set` or a sorted vector |
| how many words start with p, with inserts and deletes | trie with `pass` counters |
| autocomplete of a static dictionary | sorted vector and `lower_bound` first; a trie when the set changes or when nodes hold a best-score |
| autocomplete ranked by popularity | trie with the best score (or a top-k list) kept at each node |
| match many patterns against one text, or many words against one board | trie of the patterns or words (Word Search II, Aho-Corasick in Topic 18) |
| longest stored prefix of a key (routing tables, URL routers) | radix or level-compressed trie (Topic 18) |
| bytes or Unicode strings | sorted-vector or hash-edge children, not a 256-slot array |
| memory is tight | not a plain trie; a sorted array, a compressed trie or a succinct structure |

A systems note, stated from memory and worth checking against the sources: the Linux kernel uses tries keyed by integers rather than strings. The XArray (the successor of the radix tree, used for the page cache) is a trie of 64-way nodes indexed by the bits of a page number, and the IPv4 routing table (`fib_trie`) is a level-compressed trie on the bits of an address. Both are the subject of Topic 18; the shared idea is that a key is a sequence of small symbols and the structure is a walk down them.

---

## 8. Common mistakes

1. **Holding a reference into the node vector across an insertion.** `nodes_.emplace_back()` can move everything; the reference then points at freed memory. Take the id, call `make()`, and index again (section 3).
2. **Forgetting to mark the word end.** Without the flag, `contains("te")` is true whenever `tea` is stored; with it, `starts_with` and `contains` differ, which is the whole point.
3. **Deleting by only clearing the end flag.** The words disappear but the nodes stay: memory leaks, `starts_with` answers true for a prefix of nothing, and the node count never goes down. The fix is the `pass` counter.
4. **Freeing the wrong part on delete.** Erasing `ab` when `abc` exists must keep the path (section 3, last line of the output); erasing `abc` when `ab` exists must keep `ab`'s end flag.
5. **Indexing an array with an unchecked letter.** `c - 'a'` for an upper-case letter or a space is out of range: undefined behaviour at best. `ArrayKids` checks with `accepts`, and `insert` refuses before modifying anything.
6. **A 256-slot array for a sparse alphabet.** 1,032 bytes per node when most nodes have one child; use sorted children.
7. **Using `char` as an index.** `char` is signed on most platforms, so a byte above 127 becomes negative; the code converts to `unsigned char`.
8. **Recursive traversals on long keys.** A trie as deep as its longest key can overflow the stack (the tests use 200,000 letters); autocomplete, wildcard search and `shape_of` here use explicit stacks. Word Search II recurses, but only as deep as the longest word.
9. **Wildcard search written as if it were `O(|pattern|)`.** A dot fans out; the worst case is the node count.
10. **Calling a trie "faster than a hash table".** Exact lookup is not where it wins (section 6).
11. **Not counting memory.** At 318 to 660 bytes per word a million words take 0.3 to 0.7 GB with a 26-slot array; measure before choosing a layout.
12. **Counting a duplicate.** Inserting an existing word must not raise any `pass`; `insert` returns `false` first. (A multiset trie would count occurrences instead.)

**The eight mutants that survive**, each checked by hand: (1) `a[i] > 0` instead of `>= 0` in `ArrayKids::each`: a child id is never 0, because 0 is the root; (2) `pass <= 0` instead of `== 0` in `erase`: `pass` is never negative; (3) `ch < 0` instead of `ch <= 0` in `valid()`: a link to node 0 is caught by the "already seen" test instead; (4) dropping `limit == 0` from the first line of `complete`: the loop condition `out.size() < limit` is already false; (5) `deg <= 1` instead of `deg == 1` in `shape_of`: a childless node that is neither the root nor a word end cannot exist in a valid trie; (6) `if (true)` instead of `t.find(cur) >= 0` in `word_search`: the walk goes on from a node that `erase` has just freed, finds nothing there, and only wastes time (the answer is the same); (7) dropping the bound `b < C`: the read lands on the string's terminating `'\0'`, which matches no letter; (8) `!out.empty()` instead of `i > 0` in `replace_words`: roots are non-empty, so the two are equal.

---

## 9. Questions to answer out loud

1. How many nodes does a trie have, in terms of the words it stores? What is the largest and the smallest it can be?
2. Why is every trie operation `O(|key|)` and independent of `n`? Where does the `L` in a hash table's `O(L)` come from?
3. What does `pass` store, and how does it simplify both prefix counting and deletion?
4. Walk through erasing `tea` from a trie holding `tea`, `ted` and `to`. Which nodes are freed?
5. What are the three child layouts, and what is the memory of a node in each? When does each win?
6. Why does a 26-slot array waste about 96% of its slots on these data? What does a 256-slot array cost per node?
7. Why are the nodes held in a vector with integer ids, and what bug does the vector make possible?
8. How do you list the first ten words under a prefix in sorted order without recursion?
9. Why did the sorted vector beat the trie at autocomplete, and under what change to the problem does the trie win?
10. What fraction of the nodes have one child, and what does that suggest?

---

## 10. Problems

**A. Insert.** Insert `to tea ted ten a inn` into an empty trie. Give the node count after each insert and the `pass` of the nodes `t`, `te` and the root.

**B. Erase.** From that trie erase `ted`, then `inn`, then `to`. Give the node count after each and say which nodes were freed.

**C. Prefix word.** Insert `ab` and `abc`, then erase `ab`. How many nodes remain, and what do `contains("ab")` and `contains("abc")` answer?

**D. Memory.** The headers trie has 68,013 nodes and 23,924 words. How many bytes per word would a 256-slot array layout take? Why is that a bad layout for this data?

**E. Wildcard.** For the words `bad dad mad` answer the patterns `pad`, `.ad`, `b..` and `b.`.

**F. Word Search.** For the board `oaan / etae / ihkr / iflv` and the words `oath pea eat rain`, which words are found?

**G. Replace.** With the roots `cat bat rat`, rewrite "the cattle was rattled by the battery".

**H. Choose.** Pick a structure for each: (a) 50,000 words, exact membership only; (b) a phone keypad's autocomplete over 100,000 contacts, ten suggestions ranked by how often you call them; (c) "how many URLs in the log start with `/api/v2/`", with new URLs arriving all day; (d) the 1,000 longest prefixes of a million 32-bit addresses (look ahead to Topic 18).

---

## Answers

**A.** Node counts 3, 5, 6, 7, 8, 11 (program output): `to` creates `t` and `to`; `tea` creates `te` and `tea`; `ted` and `ten` one each; `a` one; `inn` three (`i`, `in`, `inn`). `pass` of `t` is 4 (`to tea ted ten`), of `te` 3, of the root 6 (the print is `(6 a(1*) i(1 n(1 n(1*))) t(4 e(3 a(1*) d(1*) n(1*)) o(1*)))`).

**B.** 10, then 7, then 6 (program output). `ted`: only its leaf `d` is freed, since `te` still has `tea` and `ten`. `inn`: `i`, `in` and `inn` are freed together, because `pass` of `i` fell to 0. `to`: the leaf `o` is freed; `t` stays because `tea` and `ten` pass through it.

**C.** Four nodes remain (`root a ab abc`); `contains("ab")` is false (the end flag is cleared) and `contains("abc")` is true. Nothing is freed, since `abc` still passes through every node. The print is `(1 a(1 b(1 c(1*))))`.

**D.** 68,013 times 1,032 bytes is 70.2 million bytes, over 23,924 words: about **2,934 bytes per word**, nine times the 26-slot layout's 318 and 37 times `std::set`'s 80. Of the nodes, 61% have one child and 27% have none, so almost every one of the 256 slots holds nothing.

**E.** `pad` is **no** (no word starts with `p`), `.ad` is **yes**, `b..` is **yes**, `b.` is **no** (every word has three letters). Program output: `pad 0 bad 1 .ad 1 b.. 1 b. 0`.

**F.** `oath` and `eat` (program output). `oath` is traced `o-a-t-h` down the board; `eat` is `e-a-t`. `pea` has no `p` on the board, and `rain` would need `r-a`, but no `a` is next to the `r`.

**G.** "the cat was rat by the bat" (program output): `cattle` has the root `cat`, `rattled` the root `rat`, `battery` the root `bat`; `the`, `was` and `by` have none and stay.

**H.** (a) A `std::unordered_set<std::string>`: exact membership is what it is best at, and a trie costs 4 times the memory (section 6). (b) A trie of the contacts with, at each node, the best call-count below it (or its top ten), updated when a call is logged: the query is then a walk of the typed prefix and a read, and the ranking does not need a sort. The static sorted-vector approach is not enough, because the ranking changes. (c) A trie with `pass` counters over the URL segments or letters: `count_prefix` answers in `O(|prefix|)` and a new URL costs `O(|url|)`; a sorted array would pay `O(n)` per insertion. (d) Longest-prefix match on bit strings: a binary trie, then its compressed forms. That is Topic 18.

---

## Cheat sheet

- **Trie:** one node per distinct prefix; word-end flag; `pass` counter (words through the node); every operation `O(|key|)`.
- **Children:** `ArrayKids<26>` (112-byte node, no search, 96% empty), `SortedKids` (32-byte node plus a block, binary search), `MapKids` (56 bytes plus 48 per edge, slowest). Nodes in a vector with `int` ids: no per-node allocation.
- **Insert:** `contains` first, then one walk adding to `pass`; never hold a reference across `make()`.
- **Erase:** subtract `pass` along the path, clear the flag, cut off the first node whose `pass` reached 0 and free its chain. A word that is a prefix of another only loses its flag.
- **Prefix count:** `find` plus one read: 9 to 20 ns against 377 to 605 ns for two binary searches (25 to 41 times). **Autocomplete:** stack with string-length frames; here 8 to 18 times slower than a sorted vector.
- **Wildcard:** fan out at `.`; worst case the node count. **Longest common prefix:** follow single-child, non-end nodes.
- **Word Search II:** trie of the words, one walk of the board, erase found words so the trie shrinks. **Replace Words:** the first word end on the path is the shortest root.
- **Memory per word:** 318 (array), 160 (sorted), 296 (map) against 80 (`std::set`), 78 (hash set), 32 (sorted vector) on the headers set. **Shape:** 61% of the nodes have one child, so path compression keeps 43% of them (Topic 18).
- Use a trie for **prefix questions with changing data**; use a hash set for membership and a sorted array for static ordered listing.

---

## Sources

- CLRS, *Introduction to Algorithms*, problem 12-2 (radix trees) for the basic structure; Sedgewick and Wayne, *Algorithms* (4th ed.), section 5.2 (tries, R-way and ternary search tries) for the treatment of child storage (from memory; I did not re-read either).
- Linux kernel documentation, XArray: https://docs.kernel.org/core-api/xarray.html, and Vincent Bernat, "IPv4 route lookup on Linux": https://vincent.bernat.ch/en/blog/2017-ipv4-route-lookup-linux, for the section 7 note (from memory, to be read before Topic 18).
- Striver's Trie playlist (Phase 6 of the [Trees plan](00_plan.md)) for practice on insert, search, prefix count and the maximum-XOR problem, which is Topic 18.
- LeetCode problems, by name: Implement Trie, Design Add and Search Words Data Structure, Replace Words, Word Search II, Longest Common Prefix, Map Sum Pairs, Search Suggestions System.
- Topic 09 of this series for `std::set` and `std::map`, and Topics 01 to 03 of the hashing track for the hash-table baseline.
- All other numbers in this tutorial come from the tests and measurements described at the top.
