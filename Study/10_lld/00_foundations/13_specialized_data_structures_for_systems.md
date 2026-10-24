# 13 — Specialized data structures for systems problems

> Prerequisite: `02_stl_for_system_design.md`. The problems so far have
> needed standard containers, correctly chosen. A second tier of
> machine-coding problems (autocomplete, calendar/meeting-room booking,
> dependency resolution, rate limiting, LFU caching) needs a specific,
> less-standard data structure *as the core of the design* — recognising
> which one, fast, is most of the battle. This file is a reference for
> each, not a full derivation — implement each once via the exercises,
> then treat it as a tool you reach for by name.

## Trie (prefix tree) — fast prefix matching

**Problem it solves**: given a large dictionary of strings, answer "does
any word start with this prefix?" or "give me all words starting with this
prefix" in time proportional to the **query length**, not the dictionary
size — the shape needed for typeahead/autocomplete.

```cpp
class Trie {
public:
    void insert(const std::string& word) {
        Node* cur = &root_;
        for (char c : word) {
            if (!cur->children.count(c)) cur->children[c] = std::make_unique<Node>();
            cur = cur->children[c].get();
        }
        cur->isWord = true;
    }

    bool startsWith(const std::string& prefix) const {
        return findNode(prefix) != nullptr;
    }

    std::vector<std::string> wordsWithPrefix(const std::string& prefix) const {
        std::vector<std::string> results;
        Node* start = findNode(prefix);
        if (start) collect(start, prefix, results);
        return results;
    }

private:
    struct Node {
        std::unordered_map<char, std::unique_ptr<Node>> children;
        bool isWord = false;
    };
    Node root_;

    Node* findNode(const std::string& prefix) const {
        const Node* cur = &root_;
        for (char c : prefix) {
            auto it = cur->children.find(c);
            if (it == cur->children.end()) return nullptr;
            cur = it->second.get();
        }
        return const_cast<Node*>(cur);
    }
    void collect(Node* node, std::string prefix, std::vector<std::string>& out) const {
        if (node->isWord) out.push_back(prefix);
        for (auto& [c, child] : node->children) collect(child.get(), prefix + c, out);
    }
};
```

**Complexity**: insert and prefix-lookup are both `O(L)` where `L` is the
string length — independent of how many words are stored, which is the
whole point. Space is the real cost: a naive trie (as above) can use more
memory than the raw strings, especially with a sparse alphabet — worth
naming a fix if asked ("a radix tree/compressed trie collapses chains of
single-child nodes to reduce this").

**Interview tell**: "autocomplete," "typeahead," "prefix search,"
"spell-check suggestions" → Trie.

## Segment tree / interval structures — range queries and overlap detection

**Problem it solves**: given a set of intervals (meeting times, calendar
bookings, IP ranges), answer "does this new interval overlap any existing
one?" or "what's the count/sum/max over a range?" faster than a linear scan
per query.

For the machine-coding-round version of this problem (meeting room
booking, calendar conflict detection), you rarely need a full segment tree
from scratch — the practical, interview-appropriate tool is a **sorted
structure of intervals** (`std::map<StartTime, EndTime>`) with a targeted
lookup:

```cpp
class CalendarBooking {
public:
    // Returns false (and books nothing) if [start, end) overlaps an
    // existing booking.
    bool book(int start, int end) {
        auto it = bookings_.lower_bound(start);   // first booking starting >= start

        // Check the booking starting at-or-after `start`: does it start
        // before our booking ends?
        if (it != bookings_.end() && it->first < end) return false;

        // Check the booking immediately before: does it end after our start?
        if (it != bookings_.begin()) {
            auto prev = std::prev(it);
            if (prev->second > start) return false;
        }

        bookings_[start] = end;
        return true;
    }

private:
    std::map<int, int> bookings_;   // start -> end, sorted by start
};
```

**Complexity**: `O(log n)` per booking attempt via `lower_bound`, vastly
better than an `O(n)` scan against every existing booking. This is the
right-sized answer for a 45-minute round — mention that a **segment tree**
or **interval tree** is the tool for the harder version of this problem
(overlap-*counting* across many overlapping ranges, or "how many meetings
are happening at time T" style aggregate queries) without necessarily
implementing one live unless the interviewer specifically asks for that
harder variant.

**Interview tell**: "meeting room scheduler," "calendar conflict check,"
"does this interval overlap any other" → the sorted-map approach above.
"Count overlapping intervals at any point," "range sum/max query with
updates" → segment tree, named and described, implemented only if asked
for explicitly.

## Graph adjacency structures — dependency resolution

**Problem it solves**: given a set of tasks/packages with "depends on"
relationships, determine a valid execution order (or detect that no valid
order exists because of a cycle) — Task Scheduler and Package Manager-style
problems.

```cpp
class DependencyGraph {
public:
    void addDependency(const std::string& task, const std::string& dependsOn) {
        adj_[dependsOn].push_back(task);   // edge: dependsOn -> task
        indegree_[task]++;
        indegree_.try_emplace(dependsOn, 0);   // ensure it exists even with no deps
    }

    // Kahn's algorithm: topological sort via repeated removal of zero-indegree nodes.
    // Returns an empty vector if a cycle exists (a valid order is impossible).
    std::vector<std::string> topologicalOrder() const {
        auto indegree = indegree_;
        std::queue<std::string> ready;
        for (auto& [node, deg] : indegree) if (deg == 0) ready.push(node);

        std::vector<std::string> order;
        while (!ready.empty()) {
            std::string node = ready.front(); ready.pop();
            order.push_back(node);
            auto it = adj_.find(node);
            if (it == adj_.end()) continue;
            for (auto& next : it->second) {
                if (--indegree[next] == 0) ready.push(next);
            }
        }

        return order.size() == indegree_.size() ? order : std::vector<std::string>{};
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> adj_;
    std::unordered_map<std::string, int> indegree_;
};
```

**Why Kahn's algorithm specifically, and why say so**: it gives you cycle
detection **for free** — if the final `order` has fewer nodes than the
graph, some nodes never reached indegree zero, which means a cycle exists
among them. A DFS-based topological sort also works but needs an explicit
"currently in recursion stack" set to detect cycles — Kahn's is usually the
cleaner interview answer because the cycle check falls out of the
size comparison rather than needing separate bookkeeping.

**Interview tell**: "task scheduler with dependencies," "package manager
install order," "course prerequisites," "build system dependency order" →
graph + topological sort (Kahn's).

## LFU (Least Frequently Used) cache — the harder sibling of LRU

Sketched as a "next problem" in `10_lld/01_machine_coding_problems/05_lru_cache.md`'s extensions; the full
shape, since it's asked often enough on its own to be worth having ready:
evict the **least frequently accessed** key on capacity overflow, breaking
ties by least-recently-used among equal frequencies.

**The construction that keeps both `get` and `put` at O(1)**: a
`frequency → doubly-linked-list-of-keys` map (each frequency bucket is
itself an LRU list, for tie-breaking), a `key → (value, frequency, list
iterator)` map, and a tracked `minFrequency`.

```cpp
template <typename Key, typename Value>
class LFUCache {
public:
    explicit LFUCache(size_t capacity) : capacity_(capacity) {}

    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        touch(it);
        return it->second.value;
    }

    void put(const Key& key, Value value) {
        if (capacity_ == 0) return;
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second.value = std::move(value);
            touch(it);
            return;
        }
        if (index_.size() == capacity_) evict();

        freqLists_[1].push_front(key);
        index_[key] = {std::move(value), 1, freqLists_[1].begin()};
        minFreq_ = 1;
    }

private:
    struct Entry { Value value; int freq; typename std::list<Key>::iterator it; };

    void touch(typename std::unordered_map<Key, Entry>::iterator it) {
        int freq = it->second.freq;
        freqLists_[freq].erase(it->second.it);
        if (freqLists_[freq].empty()) {
            freqLists_.erase(freq);
            if (minFreq_ == freq) ++minFreq_;
        }
        int newFreq = freq + 1;
        freqLists_[newFreq].push_front(it->first);
        it->second.freq = newFreq;
        it->second.it = freqLists_[newFreq].begin();
    }
    void evict() {
        auto& lru = freqLists_[minFreq_];
        Key victim = lru.back();
        lru.pop_back();
        if (lru.empty()) freqLists_.erase(minFreq_);
        index_.erase(victim);
    }

    size_t capacity_;
    int minFreq_ = 0;
    std::unordered_map<Key, Entry> index_;
    std::unordered_map<int, std::list<Key>> freqLists_;   // frequency -> LRU-ordered keys
};
```

**Why `minFreq_` never needs to search for a new minimum on eviction, only
on `touch()`**: every `put` of a *new* key resets `minFreq_` to 1 (a brand
new key is always the least frequent), and every `touch()` only needs to
check whether the bucket it just vacated was the current minimum — if so,
the new minimum is exactly `freq + 1`, because that's where the touched key
just moved to and buckets are contiguous by construction. This is the
specific invariant that keeps the whole structure O(1); state it explicitly
if asked "why don't you need to scan for the new minimum frequency."

### Making it concurrent

The direct application of `10_concurrency_for_lld.md`'s Monitor Object
pattern (a single mutex around every public method) is the correct default
answer, exactly as in `01_machine_coding_problems/05_lru_cache.md`'s
thread-safety extension — and here it's worth naming *why* a finer-grained
scheme is harder to justify than it was for the striped `HashMap` earlier
in this file: `LFUCache::touch()` mutates **three** things together on
every access (`index_`, the vacated frequency bucket's list, and the new
frequency bucket's list, plus possibly `minFreq_`), all of which must stay
consistent with each other. Lock striping by key (as in
`StripedHashMap`) doesn't help here, because a single access still needs
to touch two different frequency buckets that could belong to different
stripes — the invariant spans data that key-based partitioning doesn't
respect. A coarse-grained mutex is the honest, correct answer for this
structure; naming why the fine-grained technique from earlier in this same
file *doesn't* transfer is a stronger signal than reflexively trying to
apply it everywhere.

## Interview angles

**"How would you build autocomplete for a search bar?"** Trie, `O(L)`
prefix lookup, mention the memory-cost caveat and the compressed-trie fix
if pushed.

**"How would you detect a scheduling conflict / build a valid task
order?"** Sorted interval map for conflict detection; graph + Kahn's
topological sort for dependency ordering — know which one the specific
question is actually asking for, they're commonly confused.

**"Turn your LRU cache into an LFU cache."** Name the extra structure (a
frequency→list map plus a tracked minimum) before writing code — this is
exactly the "sketch the shape before implementing" discipline from
`00_foundations/16_machine_coding_framework.md`, and it's a large enough
jump from LRU that jumping straight to code usually produces a broken
`touch()`.

## Exercises

1. Implement the `Trie` above, then extend it with `startsWithCount(prefix)`
   returning the number of words with that prefix in O(L) (hint: maintain a
   count at each node incremented on every `insert` that passes through it).
2. Implement `CalendarBooking`, then extend it to `bookRecurring` (a
   weekly-recurring meeting) and identify what breaks about the sorted-map
   approach for that case — this is the moment the answer genuinely becomes
   "you'd want a proper interval tree," and being able to say precisely why
   is the actual exercise.
3. Implement `DependencyGraph` with Kahn's algorithm, then add a cycle
   deliberately and confirm `topologicalOrder()` returns an empty vector —
   write a second version using DFS with an explicit recursion-stack set
   and compare the two implementations' clarity.
4. Implement `LFUCache` from scratch, from memory, after reading this file
   once — then write a test sequence that specifically exercises a tie
   between two keys at the same frequency, confirming the LRU tie-break
   within a frequency bucket works correctly.
