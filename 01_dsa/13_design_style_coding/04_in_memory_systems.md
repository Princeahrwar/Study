# DSA 13.04: In-Memory Systems

*Part of the [Design-style coding plan](00_plan.md) · Time: about 7 hours (3.5 reading, 3.5 problems) · You need: [Cache designs](01_cache_designs.md), [Iterators and streams](02_iterators_and_streams.md) (the k-way merge) and tries from [Strings](../12_string_algorithms/00_plan.md)*

This is the last tutorial of the design-style area. The earlier ones built one structure each. Here the question names a **whole small system**: a file system, a search-suggestion box, a news feed, a key-value store with transactions. Each has an API of four to six calls, and the interview is about picking, for **each call**, the structure that makes it cheap, then proving the choices work together. The honest way to judge a design is to ask what the obvious alternative costs, so each section ends with that measured.

By the end you can:

- build an in-memory file system as a tree with ordered children, define its errors, and say why one hash map of full paths is the wrong design (measured);
- build a search autocomplete with a stored top-3 list in every trie node, say why that is correct only while counts never decrease, and compare it with scanning the subtree (measured);
- build a news feed as a k-way merge over per-user lists, and compare it with sorting everything (measured);
- build a key-value store with nested transactions and a count-by-value query using undo logs, define what `commit` means for nested transactions, and compare it with copying (measured);
- run the same checklist on any "design a small system" question: API and errors first, then one structure per operation, then the cost of the alternative.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 5,014,046 checks passed under AddressSanitizer and UBSan, which also checks that the file system frees what it removes. The tests compare each system with a model that is deliberately plain: a flat map of full paths for the file system, a scan of all sentences for autocomplete, a sort of all tweets for the feed, and a full copy of the data at every `begin` for the store. I tried 22 deliberate bugs. Nineteen were caught at once. Three escaped the first tests: two were checks that cannot change any answer (a defensive test in the path walk that is redundant because a file has no children, and an early return that `emplace` makes redundant), and one only wasted memory (counts of zero left in the value index). I removed the first from the code, replaced the second by a bug that does change answers, and added a test of the index size for the third; the 21 mutants in the final list are all caught.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. The data is synthetic and seeded (random words, random tweet lists), so treat the ratios as the shape of the effect, not as a prediction for a real workload. Statements about real products are from memory and marked as such.

---

## 1. How to approach a "design a small system" question

1. **Write the API** and the errors (what happens for a missing path, an empty transaction stack, an unknown user). Say what each call returns.
2. **Name the dominant operation** (the one called most, or the one with a latency target). Here it is `ls`, the keystroke query, the feed read, and `get`.
3. **Pick one structure per operation**, then check they can be kept consistent by the writes.
4. **State the cost** of each call and the memory.
5. **Compare with the obvious alternative** and say when it would win (small data, rare use).
6. **Test the edges:** empty, one item, a name that is also a prefix of another, the same key twice.

## 2. An in-memory file system

**API:** `ls(path)`, `mkdir(path)`, `add_content(path, text)` (creates the file or appends), `read(path)`, `rm(path)`. Paths are absolute (`/a/b/c`). `ls` of a directory lists its entries in alphabetical order; `ls` of a file returns just the file's name. `mkdir` creates the missing parents (`mkdir -p`).

**Structure:** a tree. Each node is a file (with its text) or a directory (with an **ordered map** from name to child). The ordered map makes `ls` sorted without sorting. Each operation splits the path and walks it: `O(depth * log k)` for `k` entries per directory. `rm` erases one map entry and the `unique_ptr`s free the whole subtree.

**Errors** are part of the design, so decide them first: a malformed path (no leading `/`, or a `.` / `..` component, which this version does not support) is `invalid_argument`; a missing path is `out_of_range`; reading a directory, creating a file where a directory is, or making a directory where a file is, are `invalid_argument`; removing the root is `invalid_argument`. Repeated and trailing slashes (`/a//b/`) are ignored.

```cpp
// An in-memory file system: a tree of nodes, each directory an ordered map name -> child.
// Errors: std::invalid_argument (bad path, or the wrong kind of thing there), std::out_of_range (not found).
class FileSystem {
    struct Node {
        bool is_file = false;
        std::string content;
        std::map<std::string, std::unique_ptr<Node>> kids;     // ordered, so ls is sorted for free
    };
    Node root;

    static vector<std::string> split(const std::string& path) {
        if (path.empty() || path[0] != '/') throw std::invalid_argument("path must start with /");
        vector<std::string> out;
        std::string cur;
        for (size_t i = 1; i <= path.size(); ++i) {
            if (i < path.size() && path[i] != '/') { cur += path[i]; continue; }
            if (cur.empty()) continue;                         // "//" and a trailing "/" add nothing
            if (cur == "." || cur == "..") throw std::invalid_argument("unsupported component");
            out.push_back(cur);
            cur.clear();
        }
        return out;
    }
    Node* find(const vector<std::string>& parts, size_t n) {   // the node at parts[0..n), or null (a file has no kids)
        Node* v = &root;
        for (size_t i = 0; i < n; ++i) {
            auto it = v->kids.find(parts[i]);
            if (it == v->kids.end()) return nullptr;
            v = it->second.get();
        }
        return v;
    }
    Node* make_dirs(const vector<std::string>& parts, size_t n) {   // mkdir -p of parts[0..n)
        Node* v = &root;
        for (size_t i = 0; i < n; ++i) {
            auto it = v->kids.find(parts[i]);
            if (it == v->kids.end()) it = v->kids.emplace(parts[i], std::make_unique<Node>()).first;
            else if (it->second->is_file) throw std::invalid_argument("a file is in the way");
            v = it->second.get();
        }
        return v;
    }
public:
    vector<std::string> ls(const std::string& path) {
        auto p = split(path);
        Node* v = find(p, p.size());
        if (!v) throw std::out_of_range("no such path");
        if (v->is_file) return {p.back()};                     // ls of a file is the file's name
        vector<std::string> out;
        for (auto& kv : v->kids) out.push_back(kv.first);
        return out;
    }
    void mkdir(const std::string& path) { auto p = split(path); make_dirs(p, p.size()); }
    void add_content(const std::string& path, const std::string& text) {   // creates the file, or appends
        auto p = split(path);
        if (p.empty()) throw std::invalid_argument("the root is a directory");
        Node* dir = make_dirs(p, p.size() - 1);
        auto it = dir->kids.find(p.back());
        if (it == dir->kids.end()) { it = dir->kids.emplace(p.back(), std::make_unique<Node>()).first; it->second->is_file = true; }
        else if (!it->second->is_file) throw std::invalid_argument("a directory is there");
        it->second->content += text;
    }
    std::string read(const std::string& path) {
        auto p = split(path);
        Node* v = find(p, p.size());
        if (!v) throw std::out_of_range("no such path");
        if (!v->is_file) throw std::invalid_argument("a directory");
        return v->content;
    }
    void rm(const std::string& path) {                         // a file, or a directory with everything under it
        auto p = split(path);
        if (p.empty()) throw std::invalid_argument("cannot remove the root");
        Node* dir = find(p, p.size() - 1);
        if (!dir || !dir->kids.erase(p.back())) throw std::out_of_range("no such path");
    }
};
```

```
T1 file system: mkdir(/a/b/c), add_content(/a/b/d,'hi'): ls(/a/b)=[c, d]; ls(/a/b/d)=[d]; read(/a/b/d)='hi there'; ls(/a/b/zzz)=out_of_range; read(/a/b)=invalid_argument; after rm(/a/b): ls(/a)=[]
```

(`ls(/a/b/d)` is `[d]`: the listing of a file is the file. The append worked: `'hi'` then `' there'`. `rm(/a/b)` took the directory, its subdirectory and its file with it.)

**The alternative.** A hash map from full path to content is `O(1)` for `read` and `add_content`, and tempting. But `ls` has no children list to read: it has to scan **every path** and keep those directly under the directory, and `rm` of a directory must scan for everything with the prefix. With 1,000 directories of 100 files (100,000 files), 334 calls of `ls` on a directory (ms, run 1 / run 2):

```
T6 file system with 1000 directories of 100 files (100000 files): tree and flat map give the same ls: yes
```

| Structure | Time for 334 `ls` calls |
|---|---|
| Tree with ordered children | 1.2 / 1.0 |
| One hash map of full paths | 2039.7 / 1837.6 |

The tree is about **1,700 to 1,800 times faster**, and the gap grows with the number of files. The flat map would win only if `ls` and `rm` of directories never happen. (Some real systems keep a flat map and a separate children index; that is the same two structures.) Not covered: permissions, hard and symbolic links, `..`, sizes per directory (`du` needs a size kept on each node and updated up the path, which means parent pointers).

## 3. Search autocomplete

**API (the common form):** the system is built from sentences with counts. `input(c)` receives one typed character and returns the best 3 sentences that start with everything typed so far (higher count first, ties alphabetical). The character `#` ends the sentence: it is counted once more (added if new) and the typed prefix resets.

**Structure:** a trie of the sentences. The key idea is to store, **at every node, the best 3 sentences below it**. A query is then a walk down the typed prefix (one step per character, and the walk can be kept between characters) and a copy of three names. The cost moves to the update: adding or counting a sentence touches every node on its path, and at each node the 3-element list is adjusted.

Why a short list is enough: **counts only increase.** When a sentence's count rises, it can enter a node's list (displacing the third) or move up inside it, but a sentence outside the list can never have been needed, because anything that was in the top 3 stays at least as good. If counts could drop, a sentence just pushed out could have to come back, and the stored lists would be wrong (problem E).

```cpp
// Search autocomplete: sentences with counts. After each typed character, the 3 best sentences with that
// prefix (higher count first, then alphabetical). '#' ends the sentence, which is then counted once more.
// Every trie node keeps its own best 3, so a query is a walk down the prefix and a copy of 3 names.
class Autocomplete {
    static constexpr size_t K = 3;
    struct Sent { std::string text; long long count; };
    struct Node { std::map<char, int> next; int sent = -1; vector<int> top; };
    vector<Sent> sents;
    vector<Node> nodes;
    std::unordered_map<std::string, int> id;
    int cur = 0;
    bool dead = false;                                         // the typed prefix has left the trie
    std::string prefix;

    bool better(int a, int b) const {
        return sents[a].count != sents[b].count ? sents[a].count > sents[b].count : sents[a].text < sents[b].text;
    }
    void touch(Node& n, int s) {                               // counts only grow, so a sentence can only move up
        if (std::find(n.top.begin(), n.top.end(), s) == n.top.end()) n.top.push_back(s);
        std::sort(n.top.begin(), n.top.end(), [&](int a, int b) { return better(a, b); });
        if (n.top.size() > K) n.top.resize(K);
    }
    void add(const std::string& t, long long times) {
        auto [it, fresh] = id.try_emplace(t, (int)sents.size());
        if (fresh) sents.push_back({t, 0});
        int s = it->second;
        sents[s].count += times;
        int v = 0;
        for (char c : t) {
            auto nx = nodes[v].next.find(c);
            if (nx == nodes[v].next.end()) { nodes.emplace_back(); nodes[v].next[c] = (int)nodes.size() - 1; v = (int)nodes.size() - 1; }
            else v = nx->second;
            touch(nodes[v], s);
        }
        nodes[v].sent = s;
    }
public:
    explicit Autocomplete(const vector<std::pair<std::string, int>>& init) {
        nodes.emplace_back();
        for (auto& p : init) add(p.first, p.second);
    }
    vector<std::string> input(char c) {
        if (c == '#') { if (!prefix.empty()) add(prefix, 1); prefix.clear(); cur = 0; dead = false; return {}; }
        prefix += c;
        if (!dead) {
            auto it = nodes[cur].next.find(c);
            if (it == nodes[cur].next.end()) dead = true; else cur = it->second;
        }
        vector<std::string> out;
        if (!dead) for (int s : nodes[cur].top) out.push_back(sents[s].text);
        return out;
    }
    // The same walk without typing: the stored lists of the node for `pre`.
    vector<std::string> top_stored(const std::string& pre) const {
        int v = 0;
        for (char c : pre) { auto it = nodes[v].next.find(c); if (it == nodes[v].next.end()) return {}; v = it->second; }
        vector<std::string> out;
        for (int s : nodes[v].top) out.push_back(sents[s].text);
        return out;
    }
    // The same answer without the stored lists: collect every sentence under the prefix and sort them.
    vector<std::string> top_by_dfs(const std::string& pre) const {
        int v = 0;
        for (char c : pre) { auto it = nodes[v].next.find(c); if (it == nodes[v].next.end()) return {}; v = it->second; }
        vector<int> all, st{v};
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            if (nodes[u].sent >= 0) all.push_back(nodes[u].sent);
            for (auto& kv : nodes[u].next) st.push_back(kv.second);
        }
        std::sort(all.begin(), all.end(), [&](int a, int b) { return better(a, b); });
        vector<std::string> out;
        for (size_t i = 0; i < all.size() && i < K; ++i) out.push_back(sents[all[i]].text);
        return out;
    }
};
```

```
T2 autocomplete over i love you (5), island (3), ironman (2), i love leetcode (2), typing "i a#": 'i' -> [i love you, island, i love leetcode]; ' ' -> [i love you, i love leetcode]; 'a' -> []; '#' -> [];
T2 then typing "i " again (the sentence "i a" now exists with count 1): 'i' -> [i love you, island, i love leetcode]; ' ' -> [i love you, i love leetcode, i a];
```

After `i a#` the sentence "i a" exists with count 1; it appears third once the typed prefix is `i ` (the other two have counts 5 and 2). The class also has the two other ways to answer, `top_stored(prefix)` and `top_by_dfs(prefix)` (collect every sentence under the prefix and sort); the tests check that all three agree after every character.

**The alternative.** Scan the subtree on every keystroke. 100,000 sentences (three words each from a vocabulary of 2,000), 2,000 prefixes of one or two letters:

```
T7 autocomplete over 100,000 sentences, 2,000 prefixes of 1 or 2 letters: stored lists and the full scan agree on 2000
```

| Method | Time for 2,000 queries |
|---|---|
| Stored top-3 lists | 0.3 / 0.4 |
| Scan of the subtree under the prefix | 5638.9 / 5491.5 |

The scan is more than **10,000 times slower** on these short prefixes, because a one-letter prefix sits above thousands of sentences. The price of the stored lists: building the structure for 100,000 sentences took 536.0 / 525.9 ms, and each node keeps three ids and an ordered map of children. A `std::map` per node is memory heavy; arrays of 26 or a compact edge list are the usual remedies (not measured here). Updates cost the sentence length times a sort of at most 4 items.

## 4. A news feed

**API:** `post(user, id)`, `follow(a, b)`, `unfollow(a, b)`, and `feed(user)`: the ten most recent tweets among the user's own and those of the people they follow, newest first.

**Structure:** each user's tweets in a list ordered by time (appending keeps it sorted), a set of followees per user, and a global counter as the clock. The feed is a **k-way merge** (Iterators and streams, section 7), but starting from the **newest** end of each list: a max-heap holds one candidate per followed user; pop the newest, then push that user's next older tweet. Ten pops need only ten pushes. The heap is built from all the candidates in one pass (heapify) so the setup is `O(f)` for `f` followees, and the total is `O(f + k log f)`.

```cpp
// A news feed: post, follow, unfollow, and the latest tweets of a user and the people they follow.
// Each user's tweets are a list in time order, so the feed is a k-way merge from the newest end.
class Feed {
    struct Tweet { long long time; int id; };
    std::unordered_map<int, vector<Tweet>> tweets;
    std::unordered_map<int, std::unordered_set<int>> follows;
    long long clock = 0;
public:
    void post(int user, int id) { tweets[user].push_back({clock++, id}); }
    void follow(int a, int b) { if (a != b) follows[a].insert(b); }
    void unfollow(int a, int b) { auto it = follows.find(a); if (it != follows.end()) it->second.erase(b); }
    vector<int> feed(int user, size_t k = 10) const {
        struct E { long long time; int u; size_t i; bool operator<(const E& o) const { return time < o.time; } };
        vector<E> init;
        auto add = [&](int u) {
            auto it = tweets.find(u);
            if (it != tweets.end() && !it->second.empty()) init.push_back({it->second.back().time, u, it->second.size() - 1});
        };
        add(user);
        auto f = follows.find(user);
        if (f != follows.end()) for (int u : f->second) add(u);
        std::priority_queue<E> pq(std::less<E>(), std::move(init));    // heapify: O(followees)
        vector<int> out;
        while (!pq.empty() && out.size() < k) {
            E e = pq.top(); pq.pop();
            const auto& list = tweets.at(e.u);
            out.push_back(list[e.i].id);
            if (e.i > 0) pq.push({list[e.i - 1].time, e.u, e.i - 1});
        }
        return out;
    }
    vector<int> feed_by_sorting(int user, size_t k = 10) const {      // collect everything, sort, cut
        vector<Tweet> all;
        auto take = [&](int u) { auto it = tweets.find(u); if (it != tweets.end()) all.insert(all.end(), it->second.begin(), it->second.end()); };
        take(user);
        auto f = follows.find(user);
        if (f != follows.end()) for (int u : f->second) take(u);
        std::sort(all.begin(), all.end(), [](const Tweet& a, const Tweet& b) { return a.time > b.time; });
        vector<int> out;
        for (size_t i = 0; i < all.size() && i < k; ++i) out.push_back(all[i].id);
        return out;
    }
};
```

```
T3 feed: post(1,5): feed(1)=1 tweet; after follow(1,2) and a tweet by 2: 6 5 ; after unfollow: 5
```

**The alternative** is to collect every tweet of everyone followed, sort, and keep ten. One user following 2,000 users with 500 tweets each (1,000,000 tweets), 20 feed requests:

```
T8 one user following 2000 users with 500 tweets each (1000000 tweets): heap merge and full sort give the same 10: yes
```

| Method | Time for 20 feeds |
|---|---|
| Heap merge from the newest end | 0.5 / 0.4 |
| Collect everything and sort | 747.1 / 806.2 |

The heap version is about **1,500 times faster**, because it reads about `f + 10` tweets, not a million. Where this design stops scaling: a user following a million accounts makes `O(f)` per read too slow, which is where real systems precompute (from memory: pushing each new tweet into the followers' stored feeds, "fan-out on write", for ordinary accounts, and merging at read time for accounts with huge audiences). That trade, with its storage and consistency costs, is system design territory; the code here is the read-time merge.

## 5. A key-value store with transactions

**API:** `set(key, value)`, `get(key)`, `del(key)`, `count(value)` (how many keys hold this value), and `begin()`, `rollback()`, `commit()`, possibly nested. This is a popular live-coding question (from memory).

**Semantics to state aloud.** `rollback` undoes the changes of the innermost open transaction. `commit` of an inner transaction **merges its changes into the enclosing one**, so rolling back the outer one undoes them too; `commit` of the outermost makes them permanent. (Some statements of the problem define `commit` as "make everything permanent, closing all open transactions"; the class has that as `commit_all`.)

**Structure.** A hash map for the data, a second hash map `value -> number of keys` so `count` is `O(1)`, and a stack of **undo maps**: one per open transaction, from a key to the value it had **before the transaction first touched it** (or "absent"). A write logs the old value only if this transaction has not logged that key yet. Rollback replays the undo map; a nested commit moves the inner entries into the outer map **without overwriting** (the outer one holds the older value). Every change, including the replay of an undo, goes through one function that keeps the value index right.

```cpp
// A key-value store with nested transactions and count(value). Instead of copying the data at begin(),
// each open transaction keeps an undo map: key -> the value it had before this transaction first changed it.
class TxDb {
    using Val = std::optional<std::string>;
    std::unordered_map<std::string, std::string> data;
    std::unordered_map<std::string, long long> by_value;       // value -> number of keys holding it
    vector<std::unordered_map<std::string, Val>> tx;           // one undo map per open transaction

    void apply(const std::string& k, const Val& v) {           // a raw change: keeps by_value right, logs nothing
        auto it = data.find(k);
        if (it != data.end()) { if (--by_value[it->second] == 0) by_value.erase(it->second); }
        if (v) { data[k] = *v; ++by_value[*v]; }
        else if (it != data.end()) data.erase(it);
    }
    void log(const std::string& k) {                           // remember the old value, once per key per transaction
        if (tx.empty() || tx.back().count(k)) return;
        auto it = data.find(k);
        tx.back().emplace(k, it == data.end() ? Val() : Val(it->second));
    }
public:
    void set(const std::string& k, const std::string& v) { log(k); apply(k, v); }
    void del(const std::string& k) { log(k); apply(k, Val()); }
    Val get(const std::string& k) const { auto it = data.find(k); return it == data.end() ? Val() : Val(it->second); }
    long long count(const std::string& v) const { auto it = by_value.find(v); return it == by_value.end() ? 0 : it->second; }
    void begin() { tx.emplace_back(); }
    void rollback() {
        if (tx.empty()) throw std::logic_error("no transaction");
        for (auto& kv : tx.back()) apply(kv.first, kv.second);
        tx.pop_back();
    }
    void commit() {                                            // merge into the enclosing transaction, or make permanent
        if (tx.empty()) throw std::logic_error("no transaction");
        auto top = std::move(tx.back());
        tx.pop_back();
        if (!tx.empty()) for (auto& kv : top) tx.back().try_emplace(kv.first, std::move(kv.second));   // the older value wins
    }
    void commit_all() { tx.clear(); }
    size_t distinct_values() const { return by_value.size(); }
    size_t depth() const { return tx.size(); }
    size_t logged() const { size_t n = 0; for (auto& l : tx) n += l.size(); return n; }
};
```

```
T4 transactional store: set(a,10), begin, set(a,20), begin, set(a,30): in two transactions get(a)=30, count(30)=1, count(10)=0; after rollback get(a)=20; after commit get(a)=20 at depth 0; inner commit then outer rollback: get(b)=none
T5 1,000,000 writes to one key inside one transaction: undo entries kept 1; after rollback get(k)=0
```

(The last line of `T4`: the inner commit merged `b = 2` into the outer transaction, so the outer rollback removed `b` completely.) A million writes to one key inside a transaction kept **one** undo entry; logging every write would keep a million and make rollback replay a million changes.

**The alternative** is to copy the data at `begin` and swap it back on rollback. 50 rounds of `begin`, 10 writes, `rollback` on a store of 100,000 keys (ms, run 1 / run 2):

```
T9 50 rounds of begin, 10 writes, rollback on a store of 100000 keys: after the undo version the store holds no value w (count 0); after the copy version it holds 100000 keys again
```

| Method | Time for 50 rounds |
|---|---|
| Undo maps | 0.120 / 0.129 |
| Copy the data at `begin` | 326.836 / 283.136 |

The undo log costs the number of **changes** in the transaction; the copy costs the **size of the store**, each time. Copying is correct and simple, and for a small store it is a fine first answer, but it should be named as such.

## 6. Complexities

| System | Call | Time |
|---|---|---|
| File system | `ls`, `mkdir`, `read`, `add_content`, `rm` | `O(depth * log k)` (`ls` adds the size of the listing) |
| Autocomplete | `input(c)` | `O(log alphabet)`, plus 3 names |
| Autocomplete | sentence end `#` | `O(length * 4 log 4)` |
| Feed | `post`, `follow`, `unfollow` | `O(1)` expected |
| Feed | `feed` | `O(f + k log f)` |
| Store | `set`, `del`, `get`, `count` | `O(1)` expected |
| Store | `begin`, `commit` | `O(1)`, `O(entries moved)` |
| Store | `rollback` | `O(keys changed in the transaction)` |

## 7. Common mistakes

1. **File system:** `ls` that returns the directory listing for a file (or nothing); `mkdir` that silently walks through a file; `add_content` that replaces instead of appends; a path parser that creates an empty component for `//`.
2. A file system with one map of full paths, and an `ls` that scans it.
3. **Autocomplete:** ties broken the wrong way, a stored list that is not re-sorted when a count changes, or one that keeps four; a prefix that has left the trie but keeps returning old results; `#` that does not reset the prefix.
4. Stored top-k lists in a system where counts can decrease or sentences can be removed.
5. **Feed:** a heap ordered oldest first; a user's own tweets left out; `unfollow` that does nothing; following yourself counted twice; building the heap by sorting every tweet.
6. **Store:** logging every write (not the first) and so replaying stale values; a rollback that restores the data but not the value index; a nested commit that drops the inner log, or lets the newer value overwrite the older one in the outer log; leaving zero counts in the value index.
7. Not deciding the meaning of `commit` before coding.
8. Not defining the error behaviour (what does `rollback` do with no open transaction?).
9. Forgetting that a hash map reference can be invalidated by an insert (a classic crash in the `add` of the autocomplete and in the store's `apply`).

## 8. Questions to answer out loud

1. Why does the file system keep an **ordered** map of children? What does the hash-map-of-paths design cost for `ls`?
2. What should `ls` of a file return? What errors do your calls raise?
3. Why does `rm` not need to walk the subtree by hand?
4. In the autocomplete, what is stored at each node, and what does a query cost? What does an update cost?
5. Why is a stored top-3 list correct only when counts never decrease?
6. How does the feed avoid reading every tweet? What is the cost, in `f` and `k`?
7. What is fan-out on write, and why is it not used for every account?
8. Why does the store's undo map keep only the first old value per key per transaction?
9. What does a nested `commit` do? What does the outer `rollback` do afterwards?
10. How is `count(value)` kept in sync with a rollback?
11. When would copying the data at `begin` be the better answer?
12. For each of the four systems, what is the one operation the design is built around?

## 9. Problems

### 9.1 On paper

**A.** File system: `mkdir(/x/y)`, `add_content(/x/y/f, "ab")`, `add_content(/x/g, "c")`, `add_content(/x/y/f, "d")`. Give `ls(/x)`, `ls(/x/y/f)` and `read(/x/y/f)`. Then `rm(/x/y)`: give `ls(/x)` and `ls(/x/y)`.

**B.** Autocomplete over "ab" (2), "abc" (2), "abd" (1), "b" (5). Type `a`, `b`, `c`, `#`, and then `a`, `b` again. What does each character return?

**C.** Feed: `post(1,10)`, `post(2,20)`, `post(3,30)`, `follow(1,2)`, `follow(1,3)`, `post(2,21)`. Give `feed(1)` limited to 3. Then `unfollow(1,3)` and give `feed(1)` limited to 10.

**D.** Store: `set(a,1)`, `begin`, `set(a,2)`, `begin`, `del(a)`. Give `get(a)` and `count(2)`. Then `commit`, give `get(a)` and the depth. Then `rollback`: give `get(a)` and the depth.

**E.** Why does the stored top-3 list of a trie node break if a sentence's count can be **reduced**? Describe a repair (what you would store or recompute).

**F.** Which of `/a//b/`, `a/b`, `/a/../b`, `/` does `mkdir` accept in this design?

### 9.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. Several are premium problems.

| Problem | Number | How to approach |
|---|---|---|
| Design In-Memory File System | 588 (hard, premium) | Section 2: a tree with ordered children; `ls` on a file returns its name |
| Design File System | 1166 (premium) | A map from path to value, checking that the parent exists (the flat design is enough here) |
| Design Search Autocomplete System | 642 (hard, premium) | Section 3: the trie with a top-3 list per node, or a scan if the data is small |
| Design Twitter | 355 | Section 4: per-user lists and a heap merge |
| Design Underground System | 1396 | Two maps: open trips by customer, and totals by pair of stations |
| Design A Leaderboard | 1244 (premium) | A map of scores; top K by a heap or `nth_element` |
| Design Browser History | 1472 | A vector with a cursor, or two stacks |

### 9.3 In your editor

**A.** Add `du(path)` (total size) to the file system: keep a size on every node and update it up the path on `add_content` and `rm`, which needs parent pointers.

**B.** Make the autocomplete support **removing** a sentence or lowering a count: recompute the stored lists of the nodes on its path from their children, and test against the model.

**C.** Build a **task scheduler**: tasks with priorities, a delay (use the timer wheel of 13.03) and dependencies (a topological order from the graphs area). Test against a model that scans for the best ready task.

**D.** Give the store the other meaning of `commit` (everything becomes permanent) and write the tests that tell the two meanings apart.

**E.** Make the store thread-safe with one mutex and measure it with 1, 2 and 4 threads.

### Answers

The program that checks the paper problems printed:

```
A ls(/x)=[g,y] ls(/x/y/f)=[f] read(/x/y/f)=abd after rm(/x/y): ls(/x)=[g] ls(/x/y)=missing
B 'a'=[ab,abc,abd] 'b'=[ab,abc,abd] 'c'=[abc] '#'=[] 'a'=[abc,ab,abd] 'b'=[abc,ab,abd]
C feed(1,3)=21 30 20 ; after unfollow(1,3), feed(1,10)=21 20 10
D get(a)=none count(2)=0; after commit get(a)=none depth 1; after rollback get(a)=1 depth 0
F /a//b/=ok a/b=invalid /a/../b=invalid /=ok
```

**A.** `ls(/x)` is `[g, y]`; `ls(/x/y/f)` is `[f]`; `read(/x/y/f)` is `abd` (the second `add_content` appended). After `rm(/x/y)`, `ls(/x)` is `[g]` and `ls(/x/y)` is an error: not found.

**B.** After `a` and `b` the three sentences starting with `ab` (counts 2, 2, 1) in order: `ab`, `abc` (tie on 2, alphabetical), `abd`. After `c` only `abc`. `#` returns nothing and counts "abc" once more (now 3). Typing `a` again gives `abc` first, then `ab`, then `abd`.

**C.** `feed(1)` limited to 3 is `21 30 20` (newest first, over the tweets of users 1, 2 and 3). After `unfollow(1,3)` the feed is `21 20 10`.

**D.** After the inner `del`, `get(a)` is none and `count(2)` is 0 (the value 2 was replaced by the deletion). The inner `commit` merges into the outer transaction: `get(a)` is still none and the depth is 1. The outer `rollback` restores `a = 1` and the depth is 0.

**E.** A node keeps only three ids. When a count drops, a sentence that was pushed out earlier (and is not stored anywhere in the node) may now belong in the top 3, but the node cannot know which. Repair: when a count decreases or a sentence is removed, recompute that node's list from its children's lists (and its own sentence) for every node on the path, from the deepest node upward. This is correct because a child's list already holds the best 3 below it, but a child's list must itself be correct, so the recomputation runs bottom-up. (Another repair: keep more than 3 and recompute lazily.)

**F.** Accepted: `/a//b/` (repeated and trailing slashes are ignored) and `/` (the root, nothing to create). Rejected as invalid: `a/b` (no leading slash) and `/a/../b` (`..` is not supported).

## Cheat sheet

- **Approach:** API and errors, then one structure per operation, then the cost of the obvious alternative.
- **File system:** tree with ordered children (`std::map` of `unique_ptr`); `ls` of a file is its name; `mkdir -p`; errors decided up front. A flat map of paths made 334 `ls` calls about 1,700 to 1,800 times slower.
- **Autocomplete:** trie with the best 3 stored at every node; correct only while counts never decrease; query = walk + copy 3 names. The subtree scan was more than 10,000 times slower on short prefixes.
- **Feed:** per-user lists in time order and a heap of one candidate per followee, heapified, from the newest end; `O(f + k log f)`. Sorting everything was about 1,500 times slower.
- **Store:** hash maps for the data and for `count(value)`; a stack of undo maps with the first old value per key; nested `commit` merges into the parent without overwriting; one function for every raw change. Copying at `begin` costs the size of the store each time.
- **Always:** write the edge cases (empty, same name twice, name that is a prefix of another) as tests first.

---

## Sources

- All code is tested here (5,014,046 checks under ASan and UBSan; 21 deliberate bugs all caught, after two redundant checks were removed or replaced and a test of the index size was added). Traces and timings are measured on one Xeon container, two runs each, with identical traces. The data is synthetic.
- The designs (trie with stored top-k, heap merge for a feed, undo logs for transactions) are standard; I describe them from memory and tested them here. The remarks about fan-out on write and about how real services are built are from memory and were not checked.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
