# DSA 07.18: Binary Tries, Radix Trees and Longest-Prefix Match: Maximum XOR, Patricia and Multibit Tries, Aho-Corasick

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (5 reading, 5 problems) · You need: [Tries](17_tries.md) (nodes, `pass` counters, child layouts, the `chain` count that this topic removes). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- build a **binary trie** over the bits of an integer and use it for **maximum XOR** (of two elements, of a subarray) and for counting pairs whose XOR is below a bound;
- state **longest-prefix match** (the rule of IP routing) and implement it three ways: a **unibit trie**, a **Patricia (path-compressed) trie** and a **multibit trie with controlled prefix expansion**, then say from the measurements what each one costs;
- explain **path compression** and **level compression**, and how the Linux IPv4 FIB (`fib_trie`) combines them (from the sources, not re-implemented here);
- write a **radix tree** for strings with edge splitting and merging, and prove from a measurement that it has exactly the nodes of the plain trie minus its chain nodes;
- write **Aho-Corasick** (many patterns, one pass over the text), and place **suffix trees and suffix arrays** in the same family.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 319,370 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the XOR trie against a `std::multiset` (300 runs of 40 inserts and erases, checking `max_xor`, `count_xor_less` and `contains` after each), `max_xor_pair` and `max_xor_subarray` against brute force (300 arrays, small and full 32-bit values), and the claim that a trie of `n` distinct values has `n - 1` branching nodes (100 sets); the three longest-prefix tries against a brute-force scan (400 random route tables on small address spaces, so that prefixes nest and repeat, with 60 lookups each, some chosen inside a route); the radix tree against a `std::set<std::string>` (500 runs of 50 operations, with `valid()` after every operation, sorted enumeration, and a check after every operation that its node count equals the plain trie's node count minus the plain trie's `chain` count), a word 200,000 letters long, and 12 deliberate corruptions that `valid()` must report; and Aho-Corasick against a brute-force listing of every occurrence (600 random pattern sets), plus hand cases. Finally 117 hand-made mutants of the code were run against the tests (wrong shifts, masks and comparisons, a missing split or merge, a lost fail link and so on): **113 were caught and 4 are equivalent changes** that cannot alter any output (listed after the common mistakes).

**Data and timing.** The route table is **synthetic**: 200,000 distinct routes shaped like an internet table (3% of length 8 to 15, 22% of length 16 to 23, 55% of length exactly 24, 20% of length 25 to 32), with the longer prefixes clustered inside 3,000 random /16 blocks, because real tables cluster and uniform random prefixes would exaggerate the cost of the large-node layout. The 1,000,000 lookup addresses are half random and half inside a random route. String data are the two sets of Topic 17 (23,924 identifiers from `/usr/include`, and 200,000 random strings). The pattern text for Aho-Corasick is 500,008 bytes made of randomly chosen "headers" words glued together; the patterns are the first 100, 1,000 or 5,000 words of the shuffled list. Times are nanoseconds per operation (or milliseconds per run) on a 2-core Intel Xeon VM at 2.10 GHz (Ubuntu 24.04, GCC 13.3, `-O2`), best of 5 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about the Linux kernel and about papers are from memory and say so.

---

## 1. A trie over the bits of a number, and maximum XOR

A trie does not need letters. An integer is a string of 32 bits, high bit first, and a trie over those bits has two children per node. Its nodes are exactly the distinct bit-prefixes of the stored numbers, as in Topic 17.

The classic use is **maximum XOR**: given `x`, find the stored `y` that maximises `x ^ y`. XOR is 1 where the bits differ, and a higher bit outweighs every lower bit put together, so the greedy rule is right: at each bit, from the top, go to the child **opposite** to `x`'s bit if any stored number lies there, otherwise to the same bit. The walk is 32 steps, whatever the number of values.

```cpp
// A trie over the 32 bits of an integer, high bit first.  `cnt` = how many stored values pass through a node,
//    so a value can be erased by subtracting one along its path (a node with cnt 0 counts as absent).
class XorTrie {
    struct Node { int ch[2] = {-1, -1}; int cnt = 0; };
    std::vector<Node> n_{1};
    friend struct TestAccess;
    static int bit(std::uint32_t x, int b) { return (x >> b) & 1; }
public:
    std::size_t node_count() const { return n_.size(); }
    std::size_t size() const { return n_[0].cnt; }
    void insert(std::uint32_t x) {
        int v = 0;
        ++n_[0].cnt;
        for (int b = 31; b >= 0; --b) {
            int c = bit(x, b);
            if (n_[v].ch[c] < 0) { int id = (int)n_.size(); n_.emplace_back(); n_[v].ch[c] = id; }
            v = n_[v].ch[c];
            ++n_[v].cnt;
        }
    }
    bool contains(std::uint32_t x) const {
        int v = 0;
        for (int b = 31; b >= 0; --b) { v = n_[v].ch[bit(x, b)]; if (v < 0 || n_[v].cnt == 0) return false; }
        return true;
    }
    bool erase(std::uint32_t x) {
        if (!contains(x)) return false;
        int v = 0;
        --n_[0].cnt;
        for (int b = 31; b >= 0; --b) { v = n_[v].ch[bit(x, b)]; --n_[v].cnt; }
        return true;
    }
    // max over stored y of (x ^ y): at each bit take the OPPOSITE child of x's bit if it has values
    std::uint32_t max_xor(std::uint32_t x) const {              // precondition: not empty
        int v = 0;
        std::uint32_t best = 0;
        for (int b = 31; b >= 0; --b) {
            int want = bit(x, b) ^ 1, w = n_[v].ch[want];
            if (w >= 0 && n_[w].cnt > 0) { best |= 1u << b; v = w; }
            else v = n_[v].ch[want ^ 1];
        }
        return best;
    }
    // how many stored y have (x ^ y) < k
    std::size_t count_xor_less(std::uint32_t x, std::uint32_t k) const {
        int v = 0;
        std::size_t total = 0;
        for (int b = 31; b >= 0 && v >= 0; --b) {
            int xb = bit(x, b), kb = bit(k, b);
            if (kb) {                                           // y with xor bit 0 here are all below k; go on with xor bit 1
                int same = n_[v].ch[xb];
                if (same >= 0) total += n_[same].cnt;
                v = n_[v].ch[xb ^ 1];
            } else v = n_[v].ch[xb];
            if (v >= 0 && n_[v].cnt == 0) v = -1;
        }
        return total;
    }
    std::size_t branching() const {                              // nodes with two live children
        std::size_t k = 0;
        for (const Node& u : n_) if (u.ch[0] >= 0 && u.ch[1] >= 0 && n_[u.ch[0]].cnt > 0 && n_[u.ch[1]].cnt > 0) ++k;
        return k;
    }
};

// Maximum XOR of two array elements, and of any subarray (the xor of two prefix xors).
inline std::uint32_t max_xor_pair(const std::vector<std::uint32_t>& a) {
    XorTrie t;
    std::uint32_t best = 0;
    for (std::uint32_t x : a) { if (t.size()) best = std::max(best, t.max_xor(x)); t.insert(x); }
    return best;
}
inline std::uint32_t max_xor_subarray(const std::vector<std::uint32_t>& a) {
    XorTrie t;
    t.insert(0);                                                 // the empty prefix
    std::uint32_t pre = 0, best = 0;
    for (std::uint32_t x : a) { pre ^= x; best = std::max(best, t.max_xor(pre)); t.insert(pre); }
    return best;
}
```

`cnt` plays the part of `pass` in Topic 17: how many stored values go through the node. It makes `erase` a subtraction along the path and lets every query ignore nodes whose count fell to 0 (the nodes themselves stay; a production version would free them as in Topic 17).

**`max_xor_pair`** inserts the elements one at a time, querying before each insert. **`max_xor_subarray`** uses the identity that the XOR of `a[i..j]` equals `P[j+1] ^ P[i]` for the prefix XORs `P`, so the best subarray is the best pair of prefix XORs (the empty prefix `0` is inserted first, so a subarray starting at index 0 is covered). The program prints `3 10 5 25 2 8` -> **28** (`5 ^ 25`) and `8 1 2 12 7 6` -> **15**.

**Counting.** `count_xor_less(x, k)` counts stored `y` with `x ^ y < k`. At a bit where `k` has a 1, every `y` whose XOR bit is 0 is below `k` whatever follows, so the whole subtree on the "same as x" side is added, and the walk continues on the "opposite" side; at a bit where `k` has a 0 the XOR bit must be 0 too, so the walk goes to the same side. For the values `1 2 3` and `k = 3` the pairs with XOR below 3 are `(1,3)` and `(2,3)`. Summing the query over all elements counts pairs, which solves "count pairs with XOR in a range" by taking the difference of two bounds.

**The shape.** For 200,000 random 32-bit numbers (199,998 distinct) the plain trie has **3,100,167 nodes**, 15.5 per value: the top 17 or 18 levels are shared and each value then owns a chain of about 14 nodes. At 12 bytes a node that is 37.2 million bytes (arithmetic). Those chains are exactly what path compression removes: a trie of `n` distinct values has `n - 1` nodes with two children (the program counted **199,997** for 199,998 values, and the tests check the identity on 100 random sets), so a compressed one has `n` leaves plus `n - 1` branching nodes, **2n - 1 = 399,995 nodes**: 7.8 times fewer. That is the subject of section 3.

**Against the usual alternative**, 32 passes with a hash set of prefixes (my implementation of the standard LeetCode solution), the trie took **272 ms against 1,429 ms** for the 200,000 numbers (second run: 272 against 1,141), 4.2 to 5.3 times faster, with the same answer.

---

## 2. Longest-prefix match

A router holds a table of **routes**: a prefix of an address (`10.1.0.0/16` means "the first 16 bits are `10.1`") and a next hop. An address matches every route whose first `len` bits equal its own, and it is forwarded by the **longest** matching route, so that `10.1.2.0/24` beats `10.1.0.0/16` beats `10.0.0.0/8` beats the default route `0.0.0.0/0`. Every IP forwarding path in a router, a Linux host or a software switch asks this question once per packet (the use of a trie for it is from memory, with sources at the end).

Hash tables cannot answer it directly, because the length of the match is unknown. The honest baseline is **one hash table per prefix length**, probed from the longest length present down; I measure that too.

The simplest trie is a **unibit trie**: one level per address bit, the route's next hop stored at the node where its prefix ends. A lookup walks the address bits and remembers the last next hop it passed.

```cpp
// Longest-prefix match on IPv4.  A route is (prefix, length, next hop); an address is routed by the
//    LONGEST route whose first `len` bits equal the address's first `len` bits.
inline std::uint32_t mask_of(int len) { return len == 0 ? 0u : ~0u << (32 - len); }
inline int bit_of(std::uint32_t a, int i) { return (a >> (31 - i)) & 1; }     // bit i counted from the top

// Unibit trie: one level per address bit, so a lookup is at most 32 steps; remember the last next hop seen.
class UniTrie {
    struct Node { int ch[2] = {-1, -1}; int hop = -1; };
    std::vector<Node> n_{1};
    friend struct TestAccess;
public:
    std::size_t node_count() const { return n_.size(); }
    void insert(std::uint32_t prefix, int len, int hop) {
        int v = 0;
        for (int i = 0; i < len; ++i) {
            int c = bit_of(prefix, i);
            if (n_[v].ch[c] < 0) { int id = (int)n_.size(); n_.emplace_back(); n_[v].ch[c] = id; }
            v = n_[v].ch[c];
        }
        n_[v].hop = hop;
    }
    int lookup(std::uint32_t addr, int* steps = nullptr) const {         // -1 if no route matches
        int v = 0, best = n_[0].hop, s = 0;
        for (int i = 0; i < 32; ++i) {
            v = n_[v].ch[bit_of(addr, i)];
            if (v < 0) break;
            ++s;
            if (n_[v].hop >= 0) best = n_[v].hop;
        }
        if (steps) *steps = s;
        return best;
    }
};
```

Next hops are non-negative; -1 means "none". The remembered `best` is the whole algorithm: the walk goes as deep as the trie allows, and the **last** hop seen on the way is the longest match. A lookup is at most 32 steps, and an insert creates at most `len` nodes.

For the table `0/0 -> 0`, `10/8 -> 1`, `10.1/16 -> 2`, `10.1.2/24 -> 3`, `192.168/16 -> 4`, `192.168.1.0/25 -> 5` the program prints (hop, then steps taken, for the unibit trie; the other two columns come in the next sections):

| Address | Hop | Steps, unibit | Steps, Patricia | Steps, stride 8 |
|---|---|---|---|---|
| 10.1.2.3 | 3 | 24 | 4 | 3 |
| 10.1.3.3 | 2 | 23 | 4 | 3 |
| 10.2.3.4 | 1 | 14 | 3 | 2 |
| 8.8.8.8 | 0 | 6 | 2 | 1 |
| 192.168.1.1 | 5 | 25 | 3 | 4 |
| 192.168.1.129 | 4 | 24 | 3 | 4 |

The six routes make **50 nodes** in the unibit trie (every bit of every prefix, shared where prefixes share), and the last two rows show the point of longest-prefix match: `192.168.1.1` is inside the `/25` and takes hop 5, while `192.168.1.129` is outside it and falls back to the `/16`, hop 4.

---

## 3. Path compression: the Patricia trie

Look at the 24 steps for `10.1.2.3`: most of those nodes have a single child and decide nothing. **Path compression** (Morrison's PATRICIA, 1968, from memory) replaces every chain of single-child nodes by one jump: a node stands for a prefix `(key, len)`, and its two children are *longer* prefixes, indexed by the first bit after `len`. A lookup skips the bits in between, so it must **check them** at each node: the address masked to `len` bits has to equal the node's key.

```cpp
// Path-compressed (Patricia) binary trie: a node stands for a prefix (key, len) and its two children are
//    longer prefixes, so a run of single-child nodes becomes one jump.  At most 2 * routes nodes.
class PatriciaTrie {
    struct Node { std::uint32_t key; int len; int ch[2]; int hop; };
    std::vector<Node> n_;
    int root_ = -1;
    friend struct TestAccess;
    static int common(std::uint32_t a, std::uint32_t b) { return a == b ? 32 : __builtin_clz(a ^ b); }
    int make(std::uint32_t key, int len, int hop) { n_.push_back({key & mask_of(len), len, {-1, -1}, hop}); return (int)n_.size() - 1; }
public:
    std::size_t node_count() const { return n_.size(); }
    void insert(std::uint32_t prefix, int len, int hop) {
        prefix &= mask_of(len);
        int parent = -1, pb = 0;                              // the link we are standing on: (parent node, which child), not a pointer,
        auto put = [&](int id) { if (parent < 0) root_ = id; else n_[parent].ch[pb] = id; };    //    because make() can move the vector
        while (true) {
            int u = parent < 0 ? root_ : n_[parent].ch[pb];
            if (u < 0) { put(make(prefix, len, hop)); return; }
            int c = std::min({len, n_[u].len, common(prefix, n_[u].key)});          // how many leading bits the two prefixes share
            if (c == n_[u].len && c == len) { n_[u].hop = hop; return; }
            if (c == n_[u].len) { parent = u; pb = bit_of(prefix, c); continue; }   // u is a prefix of the new route: go below u
            int w = make(prefix, c, c == len ? hop : -1);                            // otherwise split: w = the shared part
            int leaf = c == len ? -1 : make(prefix, len, hop);
            n_[w].ch[bit_of(n_[u].key, c)] = u;
            if (leaf >= 0) n_[w].ch[bit_of(prefix, c)] = leaf;
            put(w);
            return;
        }
    }
    int lookup(std::uint32_t addr, int* steps = nullptr) const {
        int u = root_, best = -1, s = 0;
        while (u >= 0) {
            const Node& x = n_[u];
            ++s;
            if ((addr & mask_of(x.len)) != x.key) break;                                  // the compressed bits must all match
            if (x.hop >= 0) best = x.hop;
            if (x.len == 32) break;
            u = x.ch[bit_of(addr, x.len)];
        }
        if (steps) *steps = s;
        return best;
    }
};
```

`insert` follows the link it is standing on, written as a (parent, which child) pair and not as a pointer: `make()` appends to a vector and may move it, so a pointer to a child slot would dangle (the same bug as in Topic 17, and the reason `put` is a lambda). At each node `u` it computes `c`, the number of leading bits the new prefix shares with `u` (capped by both lengths). Three cases: the prefixes are equal (overwrite the hop); `u` is a prefix of the new route (go below it); or they diverge at bit `c < u.len`, in which case a **split node** `w` with the shared `c` bits is created, `u` becomes one child of it and the new route the other (or `w` *is* the new route when `c == len`). Each insertion creates at most two nodes, so the trie has at most `2 * routes` nodes.

Example: `10/8`, `10.1/16`, `11/8`. The first two nest (10.1/16 hangs below 10/8). The third differs from `10/8` at the last bit of the first byte (10 is `00001010`, 11 is `00001011`), so they share 7 bits and a split node of length 7 appears with the two `/8` routes below it: the program prints **4 nodes** and the lookups `10.1.2.3 -> 2`, `11.9.9.9 -> 3`, `10.200.0.0 -> 1`, `12.0.0.0 -> -1`.

On the six-route table the Patricia trie has **6 nodes against 50**, and a lookup takes 2 to 4 steps instead of 6 to 25.

---

## 4. Level compression: multibit tries and the LC-trie

The other way to cut the number of steps is to look at **several bits per step**. A trie with an 8-bit stride has 256 slots per node and four levels for IPv4. The difficulty is a prefix whose length is not a multiple of 8, such as a `/19`: it fixes only 3 of the 8 bits of its level. **Controlled prefix expansion** (Srinivasan and Varghese, 1999, from memory) stores it in *every slot it covers*: a `/19` fixes 3 bits of its level's byte, so it is written into `2^(8-3) = 32` slots. Each slot remembers the length that wrote it, so a longer prefix always wins whatever the order of insertion.

```cpp
// Fixed-stride multibit trie (8 bits per level, so at most 4 steps): a prefix whose length is not a multiple of 8
//    is EXPANDED into every 8-bit slot it covers.  hlen remembers the length that wrote a slot, so a longer prefix
//    always wins whatever the insertion order.  Fast and large: a node is 256 slots.
class StrideTrie {
    struct Node {
        int child[256]; int hop[256]; std::uint8_t hlen[256];
        Node() { std::fill(child, child + 256, -1); std::fill(hop, hop + 256, -1); std::fill(hlen, hlen + 256, 0); }
    };
    std::vector<Node> n_{1};
    int def_ = -1;                                               // the default route, length 0
    friend struct TestAccess;
public:
    std::size_t node_count() const { return n_.size(); }
    void insert(std::uint32_t prefix, int len, int hop) {
        prefix &= mask_of(len);
        if (len == 0) { def_ = hop; return; }
        int lvl = (len - 1) / 8, v = 0;
        for (int l = 0; l < lvl; ++l) {
            int byte = (prefix >> (24 - 8 * l)) & 255;
            if (n_[v].child[byte] < 0) { int id = (int)n_.size(); n_.emplace_back(); n_[v].child[byte] = id; }
            v = n_[v].child[byte];
        }
        int rem = len - 8 * lvl;                                 // 1..8 bits of this level are fixed by the prefix
        int base = (prefix >> (24 - 8 * lvl)) & 255, span = 1 << (8 - rem);
        for (int i = 0; i < span; ++i)
            if (n_[v].hlen[base + i] <= len) { n_[v].hop[base + i] = hop; n_[v].hlen[base + i] = (std::uint8_t)len; }
    }
    int lookup(std::uint32_t addr, int* steps = nullptr) const {
        int v = 0, best = def_, s = 0;
        for (int l = 0; l < 4; ++l) {
            int idx = (addr >> (24 - 8 * l)) & 255;
            ++s;
            if (n_[v].hop[idx] >= 0) best = n_[v].hop[idx];
            v = n_[v].child[idx];
            if (v < 0) break;
        }
        if (steps) *steps = s;
        return best;
    }
};
```

The lookup does at most four array reads, and it records the best hop seen on the way down. Two details carry the correctness: the hop of a slot is read **before** descending into the slot's child (the slot's own hop is a shorter match than anything in the child), and `hlen` makes the expansion order-independent. After `10.128/9` (128 slots of the level-1 node) and then `10/8`, the program prints `10.144.0.0 -> 1` and `10.16.0.0 -> 2` with 2 nodes: the `/8` did not overwrite the longer `/9`.

The price is node size: 256 slots of 4-byte child, 4-byte hop and 1-byte length, **2,304 bytes per node** against 12 for the unibit node.

**What the Linux kernel does** (from memory; read the two sources before relying on this). The IPv4 forwarding table, `fib_trie`, is an **LC-trie** (Nilsson and Karlsson, 1999): a path-compressed binary trie in which a node near the root whose subtree is *full enough* is replaced by an array of `2^k` children, with `k` chosen per node and re-chosen as routes come and go. That combines both compressions: no chains, and fewer levels where the table is dense. Bernat's article (in the sources) measures lookup costs on real tables. The fixed 8-bit stride above is the simplest member of the family, and a "24 bits then 8 bits" two-level table is the form used by some software and hardware routers (from memory).

**The measurement.** 200,000 synthetic routes (see the top for how they were built), 1,000,000 lookups. All four methods returned the same next hop for every one of the 1,000,000 addresses (999,251 of them matched some route; the checksum of the results is the same for all four).

| Structure | Nodes | Node bytes | Memory | Steps per lookup | ns per lookup | Build, ms |
|---|---|---|---|---|---|---|
| unibit trie | 783,261 | 12 | 9.4 MB | 18.68 | 230.2 / 225.9 | 50 / 50 |
| Patricia trie | 332,357 | 20 | 6.6 MB | 16.56 | 336.4 / 348.1 | 74 / 73 |
| stride-8 trie | 45,900 | 2,304 | 105.8 MB | 2.64 | **54.4 / 43.5** | 195 / 226 |
| one hash table per length | | | 9.0 MB | 15.32 probes | 1,443.8 / 1,101.2 | |

(Memory for the three tries is nodes times node bytes, arithmetic; for the hash tables it is measured on an exact copy. The hash baseline has 25 distinct lengths, so a lookup probes up to 25 tables.)

What it shows, in order of surprise:

1. **The stride trie is 4 to 5 times faster than the unibit trie** (54 against 230, 44 against 226) and 25 to 27 times faster than the hash tables, with 2.6 steps per lookup, for **11 to 16 times the memory** of the others. On a software router that is the right trade when memory is plentiful, and the wrong one on a device with a few megabytes.
2. **Path compression did not speed up lookups here: the Patricia trie was about 1.5 times slower than the unibit trie** (336 and 348 against 230 and 226 ns), although it saves 58% of the nodes and 30% of the memory (6.6 against 9.4 MB). The steps barely fall (16.56 against 18.68): with 200,000 clustered routes most paths are dense, so there are few chains to skip, and each Patricia step does more work (a 20-byte node, a mask and a compare) than a unibit step. I did not test why the unibit trie is faster per step; the likely cause is that its top levels stay in the cache. Compression is a **memory** technique first.
3. **The per-length hash tables are the slowest** because of the 15 probes, each a hash and a cache miss, which is also why real routers do not use them.

---

## 5. Radix trees on strings

The same compression applies to Topic 17's trie: merge every chain of single-child, non-word nodes into one edge that carries a **string label**. The result is a **radix tree** (also called a compressed trie, or a Patricia tree in the string setting). Its invariants:

1. every edge label is non-empty, and the children of a node are keyed by the first byte of their label (so two children never share a first byte);
2. every node except the root is either a word end or has at least two children (otherwise it would be a chain node and must be merged).

```cpp
// A radix tree (compressed trie) holding a set of strings: every edge carries a non-empty string label, and
//    every node except the root is a word end or has at least two children.  A node's children are keyed
//    by the first byte of their label.
class RadixTree {
    struct Node { std::string label; SortedKids kids; bool end = false; };
    std::vector<Node> n_;
    std::vector<int> free_;
    std::size_t words_ = 0;
    friend struct TestAccess;
    int make() {
        if (!free_.empty()) { int id = free_.back(); free_.pop_back(); n_[id] = Node{}; return id; }
        n_.emplace_back();
        return (int)n_.size() - 1;
    }
    static std::size_t common(std::string_view a, std::string_view b) {
        std::size_t k = 0;
        while (k < a.size() && k < b.size() && a[k] == b[k]) ++k;
        return k;
    }
    std::size_t degree(int v) const { std::size_t d = 0; n_[v].kids.each([&](unsigned char, int) { ++d; }); return d; }
    int only_child(int v) const { int r = -1; n_[v].kids.each([&](unsigned char, int ch) { r = ch; }); return r; }
public:
    RadixTree() { n_.emplace_back(); }
    std::size_t size() const { return words_; }
    std::size_t node_count() const { return n_.size() - free_.size(); }

    bool insert(std::string_view s) {
        int v = 0;
        std::size_t i = 0;
        while (true) {
            if (i == s.size()) { if (n_[v].end) return false; n_[v].end = true; ++words_; return true; }
            unsigned char c = (unsigned char)s[i];
            int ch = n_[v].kids.get(c);
            if (ch < 0) {                                                  // no edge starts with this byte: a new leaf
                int leaf = make();
                n_[leaf].label = std::string(s.substr(i)); n_[leaf].end = true;
                n_[v].kids.set(c, leaf); ++words_;
                return true;
            }
            std::size_t k = common(n_[ch].label, s.substr(i));
            if (k == n_[ch].label.size()) { v = ch; i += k; continue; }    // the whole label matches: walk down
            int mid = make();                                              // otherwise split the edge after k bytes
            n_[mid].label = n_[ch].label.substr(0, k);
            n_[ch].label.erase(0, k);
            n_[mid].kids.set((unsigned char)n_[ch].label[0], ch);
            n_[v].kids.set(c, mid);
            i += k;
            if (i == s.size()) n_[mid].end = true;
            else { int leaf = make(); n_[leaf].label = std::string(s.substr(i)); n_[leaf].end = true; n_[mid].kids.set((unsigned char)s[i], leaf); }
            ++words_;
            return true;
        }
    }
    bool contains(std::string_view s) const {
        int v = 0;
        std::size_t i = 0;
        while (i < s.size()) {
            int ch = n_[v].kids.get((unsigned char)s[i]);
            if (ch < 0) return false;
            const std::string& L = n_[ch].label;
            if (s.size() - i < L.size() || s.compare(i, L.size(), L) != 0) return false;
            i += L.size(); v = ch;
        }
        return n_[v].end;
    }
    bool starts_with(std::string_view p) const {                           // the prefix may end in the middle of a label
        int v = 0;
        std::size_t i = 0;
        while (i < p.size()) {
            int ch = n_[v].kids.get((unsigned char)p[i]);
            if (ch < 0) return false;
            const std::string& L = n_[ch].label;
            std::size_t m = std::min(L.size(), p.size() - i);
            if (p.compare(i, m, L, 0, m) != 0) return false;
            i += m; v = ch;
        }
        return words_ > 0;
    }
    bool erase(std::string_view s) {
        if (!contains(s)) return false;
        std::vector<int> path{0};
        for (std::size_t i = 0; i < s.size();) { int ch = n_[path.back()].kids.get((unsigned char)s[i]); path.push_back(ch); i += n_[ch].label.size(); }
        int v = path.back();
        n_[v].end = false; --words_;
        if (v != 0 && degree(v) == 0) {                                    // a leaf: cut it off, then its parent may have become a pass-through
            int p = path[path.size() - 2];
            n_[p].kids.erase((unsigned char)n_[v].label[0]);
            free_.push_back(v);
            v = p;
        }
        if (v != 0 && !n_[v].end && degree(v) == 1) {                      // merge v with its only child: labels join
            int c = only_child(v);
            n_[v].label += n_[c].label; n_[v].kids = n_[c].kids; n_[v].end = n_[c].end;
            free_.push_back(c);
        }
        return true;
    }
    bool valid() const {
        std::vector<char> seen(n_.size(), 0);
        for (int f : free_) { if (f <= 0 || f >= (int)n_.size() || seen[f]) return false; seen[f] = 2; }
        std::vector<int> st{0};
        seen[0] = 1;
        std::size_t reach = 0, ends = 0;
        while (!st.empty()) {
            int v = st.back(); st.pop_back();
            ++reach; ends += n_[v].end;
            bool ok = true;
            std::size_t deg = 0;
            n_[v].kids.each([&](unsigned char c, int ch) {
                ++deg;
                if (ch <= 0 || ch >= (int)n_.size() || seen[ch] || n_[ch].label.empty() || (unsigned char)n_[ch].label[0] != c) { ok = false; return; }
                seen[ch] = 1; st.push_back(ch);
            });
            if (!ok || (v != 0 && !n_[v].end && deg < 2)) return false;
        }
        return reach + free_.size() == n_.size() && ends == words_;
    }
    template <class F> void each_word(F f) const {                         // in sorted order
        struct Fr { int node; std::size_t len; };
        std::vector<Fr> st{{0, 0}};
        std::string cur;
        while (!st.empty()) {
            Fr fr = st.back(); st.pop_back();
            cur.resize(fr.len);
            cur += n_[fr.node].label;
            if (n_[fr.node].end) f(cur);
            std::size_t mark = st.size();
            n_[fr.node].kids.each([&](unsigned char, int ch) { st.push_back({ch, cur.size()}); });
            std::reverse(st.begin() + mark, st.end());
        }
    }
};
```

**`insert`** walks while labels match completely. When no child starts with the next byte it adds a leaf with the rest of the word as its label. When a child's label matches only its first `k` bytes (`0 < k < label.size()`, because the child was chosen by its first byte), the edge is **split**: a new middle node takes the first `k` bytes, the old child keeps the rest as its label and becomes the middle node's child, and the word continues from the middle node (as a second child, or as the middle node itself if the word ends there). **`erase`** is the reverse: clear the end flag; if the node is now a leaf, cut it off, and if its parent is now a non-word node with one child, **merge** parent and child (concatenate the labels, take over the child's children and end flag); if the node was internal with a single child left, merge it with that child. `starts_with` may end in the middle of a label: then the whole subtree below matches, and since every leaf is a word the answer is yes.

**The example** (the standard one, as I remember it from the Wikipedia article): `romane romanus romulus rubens ruber rubicon rubicundus`.

```
(root)
  r
    om
      an
        e
        us
      ulus
    ub
      e
        ns
        r
      ic
        on
        undus
```

That is **14 nodes**; the plain trie of the same words has 28. The difference, 14, is the number of chain nodes of the plain trie, as the next measurement shows.

**The identity.** Take the `chain` count of Topic 17 (non-root, non-word nodes with exactly one child) and subtract it from the plain trie's node count: that is the radix tree's node count. The tests check this after every operation of 500 random runs. On the real data:

| | headers | random |
|---|---|---|
| plain trie nodes (sorted-vector children) | 68,013 | 1,178,634 |
| `chain` nodes | 38,859 | 927,100 |
| radix tree nodes | **29,154** | **251,534** |
| plain trie, bytes per word | 160 | 351 |
| radix tree, bytes per word | **95** | **94** |
| `contains`, ns per word: plain trie | 424.5 / 300.0 | 1,069.1 / 961.4 |
| `contains`, ns per word: radix tree | 250.5 / 214.3 | 468.0 / 409.6 |
| build, ns per word: plain trie | 573.2 / 545.1 | 1,552.4 / 1,324.1 |
| build, ns per word: radix tree | 334.5 / 307.4 | 566.6 / 469.2 |

Both node counts (29,154 and 251,534) are the numbers that Topic 17's `nodes - chain` row predicted, now reached by an implementation. The radix node is 64 bytes (a 32-byte `std::string` label, the 24-byte children vector, a flag), so the memory falls by less than the node count: 1.7 times on the headers set and 3.7 times on the random set, to about 95 bytes per word, which is close to `std::set<std::string>` (80) and far below the 26-slot array trie (318 and 660) of Topic 17. Lookups are 1.4 to 2.3 times faster and builds 1.7 to 2.8 times faster than the plain trie with the same children layout. **A radix tree is the better trie on every row measured.** Its cost is complexity: splitting, merging and the invariants above.

**Where the kernel uses this** (from memory, see the sources). The XArray, which replaced the kernel's radix tree and holds the page cache, indexes by integer: its nodes have 64 slots (6 bits per level) and the tree grows in height as larger indices appear. It is a multibit trie on the bits of the index, not a string tree, but the idea is the one of this section: skip what does not branch.

---

## 6. Aho-Corasick: many patterns, one pass

Searching a text for one pattern is KMP or `std::string::find`. Searching for **5,000 patterns** by running `find` 5,000 times costs 5,000 passes over the text. Aho and Corasick (1975, from memory) do it in **one pass whose cost does not depend on the number of patterns**, by turning the trie of the patterns into an automaton.

Put all the patterns in a trie. Reading the text, keep the node for "the longest suffix of the text read so far that is a prefix of some pattern". When the next letter has no child there, the answer is the node for the *next shorter* suffix that is still a trie node: the **fail link**, the longest proper suffix of the current node's string that is also a node. Following fail links on every miss would be slow, so `build` uses them to fill the missing transitions (`go_[v][c] = go_[fail[v]][c]`) and the search becomes one table read per text byte. To report matches that are suffixes of other matches (the pattern `he` inside the pattern `she`), each node also has a **dict link**: the nearest node along the fail chain where a pattern ends.

```cpp
// Aho-Corasick: find every occurrence of many patterns (lowercase) in a text in one pass.  A trie of the patterns,
//    plus a FAIL link per node (the longest proper suffix of the node's string that is also a trie node),
//    plus a DICT link (the nearest node along the fail chain where a pattern ends).
class AhoCorasick {
    std::vector<std::array<int, 26>> go_{std::array<int, 26>{}};   // go_[v][c]: after build(), a full automaton
    std::vector<int> fail_{0}, pat_{-1}, dict_{-1};
    bool built_ = false;
    friend struct TestAccess;
public:
    std::size_t node_count() const { return go_.size(); }
    void add(std::string_view p, int id) {                         // distinct, non-empty patterns of a..z, all before the first search
        if (built_) throw std::logic_error("AhoCorasick: add after build");
        int v = 0;
        for (char ch : p) {
            int c = ch - 'a';
            if (go_[v][c] == 0) { go_[v][c] = (int)go_.size(); go_.emplace_back(); fail_.push_back(0); pat_.push_back(-1); dict_.push_back(-1); }
            v = go_[v][c];
        }
        pat_[v] = id;
    }
    void build() {                                                 // breadth first: a node's fail link comes from its parent's
        std::vector<int> q;
        for (int c = 0; c < 26; ++c) if (go_[0][c]) q.push_back(go_[0][c]);
        for (std::size_t h = 0; h < q.size(); ++h) {
            int v = q[h];
            dict_[v] = pat_[fail_[v]] >= 0 ? fail_[v] : dict_[fail_[v]];
            for (int c = 0; c < 26; ++c) {
                int u = go_[v][c];
                if (u) { fail_[u] = go_[fail_[v]][c]; q.push_back(u); }     // follow the parent's fail link by letter c
                else go_[v][c] = go_[fail_[v]][c];                         // missing edge: borrow the fail node's, so no loop at search time
            }
        }
        built_ = true;
    }
    template <class F> void search(std::string_view text, F report) {      // report(end_index, pattern_id) for every occurrence
        if (!built_) build();
        int v = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            v = go_[v][text[i] - 'a'];
            for (int u = pat_[v] >= 0 ? v : dict_[v]; u > 0; u = dict_[u]) report(i, pat_[u]);
        }
    }
};
```

`build` is a breadth-first traversal, which is not a detail: the fail link of a node is computed from the fail link of its parent, and the parent is on an earlier level, so a depth-first order would read links that do not exist yet.

For the patterns `he she his hers` the program prints the automaton (10 nodes; `fail` is the node's longest proper suffix that is a node, `dict` the nearest pattern ending in that chain):

| Node | fail | ends pattern | dict |
|---|---|---|---|
| h | (root) | | |
| he | (root) | `he` | |
| s | (root) | | |
| sh | h | | |
| she | he | `she` | he |
| hi | (root) | | |
| his | s | `his` | |
| her | (root) | | |
| hers | s | `hers` | |

On the text `ushers` the search reports `(3,she) (3,he) (5,hers)`, where the first number is the index of the last letter of the match: `she` and `he` end at the same place, and `he` is found through `she`'s dict link. Without dict links it would be missed.

**The measurement.** 500,008 bytes of text, with the match counts of `find` in a loop (the same number every time) as the check:

| Patterns | Nodes | Matches | Build, ms | Search, ms (ns per byte) | `find` loop, ms | Speed-up |
|---|---|---|---|---|---|---|
| 100 | 595 | 1,142 | 0.04 / 0.04 | 1.28 / 1.26 (2.55 / 2.52) | 24.1 / 23.6 | 19 times |
| 1,000 | 4,872 | 19,848 | 0.41 / 0.40 | 2.86 / 2.71 (5.72 / 5.43) | 253.0 / 249.1 | 88 to 92 times |
| 5,000 | 19,386 | 85,276 | 2.17 / 2.24 | 6.51 / 6.75 (13.02 / 13.49) | 1,532.2 / 1,262.4 | 187 to 235 times |

The `find` loop grows linearly with the number of patterns (24, 253, 1,532 ms); Aho-Corasick grows much more slowly (1.3, 2.9, 6.5 ms), but not by nothing: from 2.5 to 13 ns per byte. The algorithm is `O(text + matches)`, so the growth is the memory system and the reporting (the automaton is 104 bytes a node, 2.0 MB at 5,000 patterns), which I did not separate. Where it is used: `fgrep` and similar multi-string search, and signature matching in intrusion-detection tools (from memory).

---

## 7. Suffix trees and suffix arrays (awareness)

Everything above indexes a **set of keys**. The same idea indexes **all the substrings of one text**: a **suffix tree** is the radix tree of all the suffixes of the text, with a terminator. A substring is a prefix of some suffix, so "does `p` occur in the text" is a walk of `p` from the root (`O(|p|)`), "how many times" is the number of leaves below, and the longest repeated substring is the deepest internal node. Ukkonen's algorithm (1995, from memory) builds it in `O(n)` time, and the tree has at most `2n` nodes, but each node needs several words and a map of children, so in practice it takes ten to twenty times the text size.

The **suffix array** stores only the starting positions of the suffixes in sorted order (plus, usually, the LCP array of adjacent common-prefix lengths). A query for `p` is two binary searches (`O(|p| log n)`), the memory is `4n` bytes plus the text, and it is the structure used in practice, with SA-IS or similar `O(n)` constructions (from memory). The relation to this tutorial: the suffix array is the *leaves of the suffix tree in order*; the LCP values are the branching depths between them.

This is awareness only: nothing is implemented, nothing measured and the sizes are from memory. The interview-relevant facts are the definitions, the `O(|p|)` and `O(|p| log n)` query costs, and that suffix automata and suffix arrays replace suffix trees in competitive code because they are smaller.

---

## 8. Choosing

| Need | Use |
|---|---|
| max XOR of a pair or subarray, count pairs with XOR below `k` | binary trie over the bits, high bit first |
| IP longest-prefix match, memory is plentiful | multibit (stride) trie |
| IP longest-prefix match, memory is tight, or IPv6 | Patricia or an LC-trie, with array nodes where the table is dense |
| a set of strings with prefix queries | radix tree rather than a plain trie (section 5) |
| fixed small alphabet, speed over memory, simple code | the plain trie of Topic 17 |
| many patterns in one text | Aho-Corasick |
| substring queries on one large text | suffix array (suffix tree if the `O(|p|)` bound matters) |
| plain membership | a hash set |

---

## 9. Common mistakes

1. **Walking the bits from the low end.** Maximum XOR is greedy only from the **highest** bit; the high bit outweighs all the lower ones.
2. **Signed integers.** Shifting a signed `int` and testing bit 31 invites undefined and implementation-defined behaviour; use `uint32_t`.
3. **Querying an empty XOR trie.** `max_xor` has the precondition "not empty"; `max_xor_pair` checks it with `size()`.
4. **Forgetting to mask the stored prefix.** A route `10.1.2.3/16` has host bits set; stored as is, it never equals the masked address. `insert` masks first.
5. **Taking the first match instead of the last.** Longest-prefix match is the *last* hop seen on the walk, not the first.
6. **A pointer into a vector that may move.** The Patricia insert follows a (parent, child) pair for that reason; in the string radix tree every use of a node re-indexes the vector after `make()`.
7. **Skipping the check of the compressed bits.** After a jump the lookup must compare the address with the node's key; otherwise an address that merely shares the *branching* bits would match.
8. **Expansion without a recorded length.** In a stride trie, inserting `/8` after `/9` would overwrite the `/9`'s slots. The `hlen` array is what makes the result independent of the order.
9. **A radix tree that does not merge on erase.** The invariant "a non-root, non-word node has at least two children" must be restored, or the tree drifts back toward a plain trie and `valid()` fails.
10. **Splitting at `k = 0`.** The child is chosen by its first byte, so at least one byte matches; code that splits at 0 creates an empty label.
11. **Aho-Corasick built depth first**, or without dict links (nested matches are lost), or with patterns added after `build` (the borrowed transitions then look like real children; `add` refuses).
12. **Believing that compression always speeds lookups up.** Here it saved memory, not time, for the router table (section 4).

**The mutants that survive**, each checked by hand: (1) dropping the `cnt == 0` guard in `XorTrie::count_xor_less`: stepping into an empty child adds zero to the answer, so the result is the same; (2) dropping `prefix &= mask_of(len)` at the top of `PatriciaTrie::insert`: `make` masks the key again, and the shared-bit count `common` is clamped to `len`, so stray low bits are never read; (3) dropping the `len == 0` shortcut in `StrideTrie::insert`: a /0 route is then expanded into all 256 slots of the first node with `hlen` 0, and any longer prefix overrides it, so lookups return the same hop; (4) dropping the length guard in `RadixTree::contains`: `std::string::compare` clamps to the end of the string, so a label longer than the rest of the word still compares unequal.

---

## 10. Questions to answer out loud

1. Why is the greedy "take the opposite bit" walk correct for maximum XOR? Why from the high bit?
2. How do you find the maximum XOR of a subarray with a trie of prefix XORs?
3. How many nodes does a trie of `n` distinct 32-bit values have at most, and how many does the path-compressed one have?
4. State longest-prefix match. Why can a hash table not answer it directly, and what is the hash-per-length workaround?
5. What does a Patricia node store, and why must a lookup compare the skipped bits?
6. Walk through inserting `10/8`, `10.1/16`, `11/8` into a Patricia trie.
7. What is controlled prefix expansion? How many slots does a `/19` take in an 8-bit stride trie, and what happens when `/8` is inserted after `/9`?
8. What did the measurements say about the Patricia trie against the unibit trie? What did they say about the stride trie?
9. State the invariants of a radix tree. How does `erase` restore them?
10. Why is the radix tree's node count exactly the plain trie's node count minus its chain count?
11. What are the fail link and the dict link of Aho-Corasick? Why is `build` breadth first?
12. What is a suffix array, and what does a query cost?

---

## 11. Problems

**A. Maximum XOR.** Find the maximum XOR of two numbers in `3 10 5 25 2 8`, and say which pair gives it. Then the maximum XOR of a subarray of `8 1 2 12 7 6`.

**B. Counting.** How many pairs `i < j` of `1 2 3` have `a[i] ^ a[j] < 3`? Describe how `count_xor_less` answers it for one element.

**C. Longest match.** With the six routes of section 2, which hop does each of `10.1.3.3`, `8.8.8.8` and `192.168.1.129` take? Which route is the match each time?

**D. Patricia.** Insert `10/8`, `10.1/16`, `11/8` into a Patricia trie. How many nodes? Which prefix does the extra node stand for?

**E. Expansion.** How many slots of an 8-bit stride trie does each of `/8`, `/9`, `/19`, `/24` and `/25` occupy (and in which level)? Why is `/25` in level 3 and `/24` in level 2?

**F. Radix tree.** Build the radix tree of `test team toast` and count the nodes. Then erase `team` and redraw it.

**G. Aho-Corasick.** For the patterns `a ab bab` give the nodes, the fail links and the matches in the text `abab`.

**H. Choose.** Pick a structure for each: (a) a software router with a million IPv4 routes, 10 million lookups per second and 512 MB to spare; (b) the same router on a switch chip with 4 MB; (c) scanning a 2 GB log for 20,000 known bad strings; (d) a file-path index with prefix queries and frequent creates and deletes; (e) the longest repeated substring of a 100 MB genome.

---

## Answers

**A.** **28**, from `5 ^ 25` (`00101 ^ 11001 = 11100`). For the subarray, the prefix XORs of `8 1 2 12 7 6` are `0 8 9 11 7 0 6`; the best pair is `8 ^ 7 = 15`, which is the subarray between those two prefixes, `1 2 12` (`1 ^ 2 ^ 12 = 15`). Program output: 28 and 15.

**B.** Two: `(1,3)` with XOR 2 and `(2,3)` with XOR 1 (`1 ^ 2 = 3` is not below 3). For one element `x` the query walks the 32 bits of `k = 3` (`...011`): at the two bits where `k` has a 1, it adds the count of the subtree on the side that makes the XOR bit 0 and goes to the other side; at the higher bits where `k` has 0 it must keep the XOR bit 0.

**C.** `10.1.3.3` takes hop **2** (the `10.1.0.0/16` route; the `/24` is `10.1.2.0`, not `10.1.3`); `8.8.8.8` takes hop **0** (only the default route matches); `192.168.1.129` takes hop **4** (the `/16`; the `/25` covers `.0` to `.127`). Program output in the table of section 2.

**D.** **4 nodes**: `10/8`, `10.1/16` (below it), `11/8`, and a split node for the 7 bits they share, `10.0.0.0/7`, which is not a route (no next hop) but exists because the two `/8` prefixes diverge at bit 8. Program output: 4 nodes; lookups `10.1.2.3 -> 2`, `11.9.9.9 -> 3`, `10.200.0.0 -> 1`, `12.0.0.0 -> -1`.

**E.** A prefix of length `len` is stored at level `(len - 1) / 8` and fills `2^(8 - rem)` slots, `rem = len - 8 * level`. So `/8`: level 0, 1 slot. `/9`: level 1, `rem = 1`, **128** slots. `/19`: level 2, `rem = 3`, **32** slots. `/24`: level 2, `rem = 8`, 1 slot. `/25`: level 3, `rem = 1`, **128** slots. A `/24` fixes the first three bytes, so it lives in the node reached by two bytes and is selected by the third; a `/25` also fixes one bit of the fourth byte, so it needs the fourth level.

**F.** `test` and `team` share `te`, and all three words share `t`, so the tree is root -> `t` -> {`e` -> {`st`, `am`}, `oast`}: root, `t`, `e`, `st`, `am` and `oast`, **6 nodes**. Erasing `team` removes the leaf `am`; `e` now has one child (`st`) and is not a word, so it merges with it: root -> `t` -> {`est`, `oast`}, **4 nodes** (the leaf `am` and the merged `e` are gone). (The verifier rebuilds both trees independently.)

**G.** Nodes: `a`, `ab`, `b`, `ba`, `bab` (plus the root). Fail links: `a` -> root, `ab` -> `b`, `b` -> root, `ba` -> `a`, `bab` -> `ab`. Patterns end at `a`, `ab` and `bab`; the dict link of `bab` is `ab` (its suffix `ab` is a pattern), of `ab` it is none, and of `ba` it is `a`. In `abab` the matches (end index, pattern) are `(0,a)`, `(1,ab)`, `(2,a)`, `(3,ab)`, `(3,bab)`; the last two come from the same position, `bab` through the node and `ab` through the dict link. (Checked by brute force in the verifier.)

**H.** (a) A stride trie or an LC-trie. The stride trie took 106 MB for 200,000 synthetic routes (section 4), so the memory for a million routes has to be checked before choosing it; an LC-trie keeps the array nodes only where the table is dense. (b) A path-compressed trie: 6.6 MB for 200,000 routes in the measurement, so 4 MB for a million routes needs more compression than any structure in this tutorial (which is why hardware routers use other methods, from memory). (c) Aho-Corasick: one pass over 2 GB whatever the number of patterns (section 6); check the automaton size first (104 bytes a node). (d) A radix tree: prefix queries, dynamic updates, and about 95 bytes per word against 160 to 350 for the plain trie. (e) A suffix array with the LCP array, or a suffix automaton: the longest repeated substring is the largest LCP value.

---

## Cheat sheet

- **Binary trie:** 32 levels over the bits, high bit first, `cnt` per node. **Max XOR:** take the opposite child when it is live. **Subarray:** max XOR of two prefix XORs. **Count XOR < k:** at a 1-bit of `k` add the same-side subtree and go opposite.
- **Shape:** `n` distinct values give `n - 1` branching nodes; compressed: `2n - 1` nodes (399,995 for 199,998 values, against 3,100,167 uncompressed).
- **Longest-prefix match:** the last hop seen on the walk; mask prefixes on insert. **Unibit:** at most 32 steps. **Patricia:** nodes `(key, len)`, check the skipped bits, at most `2 * routes` nodes (332,357 against 783,261 nodes, but 1.5 times slower here). **Stride 8:** at most 4 steps, controlled prefix expansion with a stored length (54 against 230 ns, 106 against 9 MB). **LC-trie:** both compressions (`fib_trie`, from memory).
- **Radix tree:** non-empty labels, children keyed by first byte, non-root nodes are word ends or branch. Split on insert, merge on erase. Nodes = plain trie nodes minus chain nodes (29,154 and 251,534); 95 bytes per word, 1.4 to 2.4 times faster lookups than the plain trie.
- **Aho-Corasick:** trie + fail links (BFS) + dict links; `O(text + matches)`; 19 to 235 times faster than a `find` loop for 100 to 5,000 patterns.
- **Suffix tree / array:** all suffixes; `O(|p|)` and `O(|p| log n)` queries; the array is the practical one.

---

## Sources

- Vincent Bernat, "IPv4 route lookup on Linux": https://vincent.bernat.ch/en/blog/2017-ipv4-route-lookup-linux (the LC-trie behind the IPv4 FIB); XArray documentation: https://docs.kernel.org/core-api/xarray.html. The kernel statements in sections 4 and 5 are from memory and should be checked against these before an interview.
- Nilsson and Karlsson, "IP-address lookup using LC-tries" (1999); Srinivasan and Varghese, "Fast address lookups using controlled prefix expansion" (1999); Morrison, "PATRICIA, practical algorithm to retrieve information coded in alphanumeric" (1968); Aho and Corasick, "Efficient string matching: an aid to bibliographic search" (1975); Ukkonen, "On-line construction of suffix trees" (1995). All cited from memory; I did not re-read them.
- CLRS, *Introduction to Algorithms*, problem 12-2 (radix trees); Sedgewick and Wayne, *Algorithms*, section 5.2 (from memory).
- LeetCode, by name: Maximum XOR of Two Numbers in an Array, Maximum XOR With an Element From Array, Count Pairs With XOR in a Range, Stream of Characters (Aho-Corasick), Implement Trie, Design Add and Search Words Data Structure.
- Striver's Trie playlist (Phase 6 of the [Trees plan](00_plan.md)) for the maximum-XOR problems.
- Topic 17 of this series for the plain trie, `pass` counters and `shape_of`; all other numbers in this tutorial come from the tests and measurements described at the top.
