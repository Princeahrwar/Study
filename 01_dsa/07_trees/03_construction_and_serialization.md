# DSA 07.03: Construction and Serialization

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (3 reading, 5 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (the three orders, the n + 1 null links) and [Iterative traversals and Morris traversal](02_iterative_traversals_and_morris.md) (the explicit stack, the state-per-frame idea) · Related: [Hash tables inside](../02_hashing/01_hash_tables_inside.md) (the position map) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- say which pairs of traversals identify a binary tree and which do not, with a counterexample for each failure;
- build a tree from preorder and inorder, or postorder and inorder, in `O(n)` with a position map, and without a map or recursion with a stack;
- serialize a tree to text in preorder and in level order, to a compact binary form, and rebuild it, without recursion;
- check that a string is a complete preorder serialization without building the tree;
- test subtree equality with serialization (and name the three mistakes that break it) and exactly with canonical subtree ids.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 3,115,076 checks passed under AddressSanitizer with UBSan (GCC 13.3). The checks cover every tree shape of 0 to 8 nodes with six random labellings each, random trees of up to 2000 nodes with the smallest and largest `int` as values, chains of 20,000 nodes (recursive builders) and one million nodes (iterative code), random token strings against a reference parser, and random subtree questions against a brute force. The traces come from a separate short Python model. Counts and sizes are exact. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`. The LeetCode problems are summarised from memory; titles were checked (see Sources) and no difficulty labels are given because none could be verified.

All listings need `<cstdint>`, `<deque>`, `<map>`, `<optional>`, `<stdexcept>`, `<string>`, `<tuple>`, `<unordered_map>`, `<utility>` and `<vector>`, and use `using Ints = std::vector<int>;`. Node values are `int`; the builders also need the values in one sequence to be distinct (section 1.3). The nodes live in an arena, so there is nothing to free node by node:

```cpp
struct Node {
    int val;
    Node* left = nullptr;
    Node* right = nullptr;
};

struct Arena {                                   // owns every node; no per-node delete
    std::deque<Node> nodes;
    Node* make(int v) {
        nodes.push_back(Node{v});
        return &nodes.back();
    }
};
```

The running example is the tree T of tutorial 02. Its preorder is 1 2 3 4 5 6 7, its inorder 3 2 5 4 1 6 7 and its postorder 3 5 4 2 7 6 1.

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

## 1. What a traversal sequence tells you

### 1.1 Rebuilding is the inverse of traversing

A traversal turns a tree into a list. **Construction** goes the other way: from given lists, build the tree. **Serialization** is the same problem with the freedom to choose the list: write the tree to a string or bytes, and read it back. Both have one test, the **round trip**: the rebuilt tree must equal the original, node for node.

### 1.2 Preorder plus inorder

In preorder the first value is the root. In inorder everything left of the root's position is the left subtree and everything right of it is the right subtree. The sizes of the two parts are now known, so the next values of the preorder split the same way, and the argument repeats on each part. Every step fixes one node, so the pair identifies one tree. The same argument works for postorder plus inorder, with the root at the end of the postorder.

### 1.3 Two conditions and one failure

- **The values must be distinct.** The position of the root in the inorder is found by its value. With equal values the position is not unique. Example: two nodes with value 1, one tree with the second as a left child and one with it as a right child; both have preorder 1 1 and inorder 1 1. (This is tested.)
- **Inorder is required.** Preorder plus postorder do not identify a tree: the root with only a left child 2 and the root with only a right child 2 both have preorder 1 2 and postorder 2 1. A lone child could be on either side, and neither sequence says which. What is lost is exactly this: for a node with one child, which side it is on.

How much is lost? I generated every tree shape with `n` nodes, labelled the nodes by their preorder number, and counted the different postorders. Two shapes collide exactly when they have the same preorder and the same postorder.

| `n` | Shapes (Catalan) | Different (preorder, postorder) pairs | Shapes in which every node has 0 or 2 children |
|---|---|---|---|
| 1 | 1 | 1 | 1 |
| 2 | 2 | 1 | 0 |
| 3 | 5 | 2 | 1 |
| 4 | 14 | 4 | 0 |
| 5 | 42 | 9 | 2 |
| 6 | 132 | 21 | 0 |
| 7 | 429 | 51 | 5 |
| 8 | 1430 | 127 | 0 |
| 9 | 4862 | 323 | 14 |

The middle column is 1, 1, 2, 4, 9, 21, 51, 127, 323, which matches the Motzkin numbers (I recognised the sequence and checked it against the Motzkin recurrence; I did not prove the match). The last column is the Catalan number of `(n - 1) / 2`. When every node has 0 or 2 children, preorder plus postorder do identify the tree (the tests check this), which is what LeetCode 889 relies on.

---

## 2. Building from preorder and inorder

The recursive builder takes the inorder range `[lo, hi)` of the subtree it must build. It takes the next unused preorder value as the root, looks up the root's position `m` in the inorder, and builds the left subtree from `[lo, m)` and the right one from `[m + 1, hi)`. The order of the two calls matters: preorder lists the left subtree first.

```cpp
struct PreInBuilder {
    Arena& arena;
    const Ints& pre;
    std::unordered_map<int, int> pos;            // value -> index in inorder
    size_t next = 0;                             // next unused index in preorder

    PreInBuilder(Arena& a, const Ints& p, const Ints& in) : arena(a), pre(p) {
        for (int i = 0; i < (int)in.size(); ++i) pos[in[i]] = i;
    }
    Node* build(int lo, int hi) {                // the subtree whose inorder is in[lo, hi)
        if (lo >= hi) return nullptr;
        Node* n = arena.make(pre[next++]);       // preorder: the root comes first
        int m = pos.at(n->val);                  // its place splits inorder in two
        n->left = build(lo, m);                  // left subtree comes first in preorder
        n->right = build(m + 1, hi);
        return n;
    }
};

inline Node* buildPreIn(Arena& arena, const Ints& pre, const Ints& in) {
    PreInBuilder b(arena, pre, in);
    return b.build(0, (int)in.size());
}
```

The map from value to inorder index is what makes it `O(n)`: without it every call searches the range for the root. Trace on T. `[lo, hi)` is the inorder range, `m` the root's position in `3 2 5 4 1 6 7`:

| Root (next preorder value) | Range `[lo, hi)` | `m` | Left range | Right range |
|---|---|---|---|---|
| 1 | `[0, 7)` | 4 | `[0, 4)` | `[5, 7)` |
| 2 | `[0, 4)` | 1 | `[0, 1)` | `[2, 4)` |
| 3 | `[0, 1)` | 0 | empty | empty |
| 4 | `[2, 4)` | 3 | `[2, 3)` | empty |
| 5 | `[2, 3)` | 2 | empty | empty |
| 6 | `[5, 7)` | 5 | empty | `[6, 7)` |
| 7 | `[6, 7)` | 6 | empty | empty |

The roots come out in the order 1 2 3 4 5 6 7: the builder walks the preorder once. With the map, the time is `O(n)` and the space `O(n)` for the map plus `O(h)` for the recursion. The same builder with a linear search for the root took 2.09 ms for a left-leaning chain of 5000 nodes and 34.92 ms for 20,000 nodes (four times the nodes, 17 times the time), against 0.20 ms and 1.01 ms with the map (best of 3).

**Postorder plus inorder** is the mirror image. Read the postorder from the back: the last value is the root, then come the right subtree and the left subtree in reverse. So the right subtree must be built first.

```cpp
struct PostInBuilder {
    Arena& arena;
    const Ints& post;
    std::unordered_map<int, int> pos;
    int next;                                    // next unused index in postorder, from the back

    PostInBuilder(Arena& a, const Ints& p, const Ints& in) : arena(a), post(p), next((int)p.size() - 1) {
        for (int i = 0; i < (int)in.size(); ++i) pos[in[i]] = i;
    }
    Node* build(int lo, int hi) {
        if (lo >= hi) return nullptr;
        Node* n = arena.make(post[next--]);      // postorder read backwards: root, right, left
        int m = pos.at(n->val);
        n->right = build(m + 1, hi);             // so the right subtree is built first
        n->left = build(lo, m);
        return n;
    }
};

inline Node* buildPostIn(Arena& arena, const Ints& post, const Ints& in) {
    PostInBuilder b(arena, post, in);
    return b.build(0, (int)in.size());
}
```

Swapping the two calls is the classic mistake here (section 8).

---

## 3. Building without recursion or a map

The recursive builder has two costs: the call depth is the height `h` (a chain of a million nodes would overflow the stack, tutorial 02 section 1), and the hash map is a large part of the time. A stack builds the same tree in one pass over the preorder.

The idea: while the next preorder value is not yet at its inorder place, it is the left child of the node on top of the stack. When the node on top *is* the next value of the inorder, its left part is finished; close it, and keep closing while the next inorder value is on top. The next preorder value is then the right child of the last node closed.

```cpp
inline Node* buildPreInIter(Arena& arena, const Ints& pre, const Ints& in) {
    if (pre.empty()) return nullptr;
    Node* root = arena.make(pre[0]);
    std::vector<Node*> st{root};                 // nodes still open: their right child is not placed
    size_t j = 0;                                // the next inorder value to be "closed"
    for (size_t i = 1; i < pre.size(); ++i) {
        Node* n = arena.make(pre[i]);
        if (st.back()->val != in[j]) {
            st.back()->left = n;                 // the top has not reached its inorder place yet
        } else {
            Node* last = nullptr;                // close every node whose inorder place has come
            while (!st.empty() && st.back()->val == in[j]) {
                last = st.back();
                st.pop_back();
                ++j;
            }
            last->right = n;                     // n is the right child of the last one closed
        }
        st.push_back(n);
    }
    return root;
}
```

Trace on T. `j` is the index of the next inorder value, which starts as 3. The stack is shown bottom to top:

| Next preorder value | Stack before | What happens | Stack after |
|---|---|---|---|
| 2 | 1 | top 1 is not 3: `1.left = 2` | 1 2 |
| 3 | 1 2 | top 2 is not 3: `2.left = 3` | 1 2 3 |
| 4 | 1 2 3 | top 3 equals 3: close 3 (next inorder is 2), close 2 (next is 5); `2.right = 4` | 1 4 |
| 5 | 1 4 | top 4 is not 5: `4.left = 5` | 1 4 5 |
| 6 | 1 4 5 | close 5, 4, 1 (next is 6); `1.right = 6` | 6 |
| 7 | 6 | close 6; `6.right = 7` | 7 |

Each value is pushed and popped once, so the time is `O(n)`, the extra space is the stack (at most `h`) and no map is needed. It assumes valid, matching input with distinct values; the recursive version with the map at least throws (`at`) when a value is missing. On a random tree of one million nodes, the recursive builder with the map took 345.7 ms and the stack builder 16.8 ms (best of 3, same machine). The stack builder also built a left-leaning chain of one million nodes, where recursion has no chance.

---

## 4. Preorder plus postorder

For a tree in which every node has 0 or 2 children, the tree is determined. The root is `pre[0]` and `post` ends with it. If there are more nodes, `pre[1]` is the root of the left subtree, and in the postorder that root is the *last* node of the left block. Its position tells how many nodes the left subtree has, and so where the right block starts in both sequences.

```cpp
struct PrePostBuilder {
    Arena& arena;
    const Ints& pre;
    const Ints& post;
    std::unordered_map<int, int> postPos;

    PrePostBuilder(Arena& a, const Ints& pr, const Ints& po) : arena(a), pre(pr), post(po) {
        for (int i = 0; i < (int)po.size(); ++i) postPos[po[i]] = i;
    }
    // the subtree occupies pre[p, p + len) and post[q, q + len)
    Node* build(int p, int q, int len) {
        if (len == 0) return nullptr;
        Node* n = arena.make(pre[p]);
        if (len == 1) return n;
        int leftSize = postPos.at(pre[p + 1]) - q + 1;   // pre[p+1] is the left root; it ends the left block of post
        n->left = build(p + 1, q, leftSize);
        n->right = build(p + 1 + leftSize, q + leftSize, len - 1 - leftSize);
        return n;                                        // a lone child always becomes the left one
    }
};

inline Node* buildPrePost(Arena& arena, const Ints& pre, const Ints& post) {
    PrePostBuilder b(arena, pre, post);
    return b.build(0, 0, (int)pre.size());
}
```

For a node with a single child the data are ambiguous (section 1.3), and the code puts the child on the left. The result still has the given preorder and postorder, which the tests check on every shape up to 8 nodes; it equals the original when the original is full. This is LeetCode 889, which accepts any valid tree.

---

## 5. Serialization

### 5.1 What a format needs

- **It must mark the missing children.** A tree with `n` nodes has `n + 1` null links (tutorial 01, section 4.4). Write one marker per null link, and the preorder alone identifies the tree. Without the markers, 1 2 3 describes many trees.
- **It must be self-delimiting.** The reader must know where each number ends and where the tree ends. A marker per null link does the second: the tree is complete when every slot is filled.
- **It should not need the call stack**, or it crashes on the trees of tutorial 02 section 1.

### 5.2 Preorder text

Write each value followed by a comma, and `#,` for a missing child. The writer is the iterative preorder of tutorial 02 with the null pointers pushed too. The reader is the interesting part. It must attach each new node to the first open slot, in preorder, without recursion. Keep a stack of nodes that are still waiting for a child, with a counter of how many children they have received (`filled`). A token goes into the slot of the top node; if the token is a node, it is pushed to wait for its own children; when the top node has both children it is popped.

```cpp
inline std::string serialize(const Node* root) {     // preorder with "#" for a missing child
    std::string out;
    std::vector<const Node*> st{root};
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        if (!n) {
            out += "#,";
            continue;
        }
        out += std::to_string(n->val);
        out += ',';
        st.push_back(n->right);                      // right first, so the left pops first
        st.push_back(n->left);
    }
    return out;
}

template <class Next>        // Next() gives the next value, or nullopt for a missing child
Node* rebuild(Arena& arena, Next next) {
    struct Frame { Node* n; int filled; };           // filled: 0 = left is next, 1 = right is next
    std::vector<Frame> st;
    std::optional<int> first = next();
    if (!first) return nullptr;
    Node* root = arena.make(*first);
    st.push_back({root, 0});
    while (!st.empty()) {
        std::optional<int> v = next();
        Node* n = v ? arena.make(*v) : nullptr;
        Frame& f = st.back();
        if (f.filled == 0) {
            f.n->left = n;
            f.filled = 1;
        } else {
            f.n->right = n;
            st.pop_back();                           // both children placed: the node is closed
        }
        if (n) st.push_back({n, 0});
    }
    return root;
}

inline Node* deserialize(Arena& arena, const std::string& s) {
    size_t i = 0;
    return rebuild(arena, [&]() -> std::optional<int> {
        if (i >= s.size()) throw std::runtime_error("truncated input");
        size_t j = s.find(',', i);
        if (j == std::string::npos) throw std::runtime_error("missing comma");
        std::string tok = s.substr(i, j - i);
        i = j + 1;
        if (tok == "#") return std::nullopt;
        return std::stoi(tok);
    });
}
```

`rebuild` takes a function `Next` that supplies the next value or "missing", so the same code reads text here and bits in section 5.5. For T the string is

`1,2,3,#,#,4,5,#,#,#,6,#,7,#,#,`

and the reader stops by itself after the last `#`, because the stack is empty. A truncated string makes `deserialize` throw instead of reading past the end. The writer and the reader are `O(n)` and use `O(h)` extra space. I measured one million random nodes (best of 3): `serialize` 178.2 ms and `deserialize` 64.8 ms for 8,888,892 bytes. The serializer is the slower side here; I did not profile why or try to tune it.

### 5.3 Checking a string without building the tree

Because there are `n + 1` null links, a valid string has one more `#` than it has numbers, and no proper prefix is already complete. Count the **open slots**: start with 1 (the root). Each token fills one slot, and a number opens two more. The string is valid exactly when the slots reach 0 at the last token and not before. This is LeetCode 331.

```cpp
inline bool validPreorder(const std::string& s) {    // is s a complete preorder serialization?
    int slots = 1;                                   // children still to be placed
    size_t i = 0;
    while (i < s.size()) {
        if (slots == 0) return false;                // the tree was complete before the end
        size_t j = s.find(',', i);
        if (j == std::string::npos) return false;
        bool isNull = (s.compare(i, j - i, "#") == 0);
        i = j + 1;
        --slots;                                     // this token fills one slot
        if (!isNull) slots += 2;                     // a node opens two new ones
    }
    return slots == 0;
}
```

The tests compared it with a reference parser (the `rebuild` above with a check that all tokens are consumed) on 4000 random token strings.

### 5.4 Level order

Level order is the format of most tree questions. Write the tree breadth first with a marker for each missing child, then drop the markers at the end of the string, because the reader treats a missing token as "missing child". For T it is

`1,2,6,3,4,#,7,#,#,5,`

(a LeetCode statement would write this as `[1,2,6,3,4,null,7,null,null,5]`). The reader keeps a queue of nodes whose children are not read yet and takes two tokens for each:

```cpp
inline std::string serializeLevel(const Node* root) {
    std::string out;
    std::deque<const Node*> q{root};
    while (!q.empty()) {
        const Node* n = q.front();
        q.pop_front();
        if (!n) {
            out += "#,";
            continue;
        }
        out += std::to_string(n->val);
        out += ',';
        q.push_back(n->left);
        q.push_back(n->right);
    }
    while (out.size() >= 2 && out.compare(out.size() - 2, 2, "#,") == 0) out.resize(out.size() - 2);
    return out;                                      // trailing "#," pairs are implied
}

inline Node* deserializeLevel(Arena& arena, const std::string& s) {
    size_t i = 0;
    auto next = [&]() -> std::optional<int> {
        if (i >= s.size()) return std::nullopt;      // missing tokens at the end are nulls
        size_t j = s.find(',', i);
        std::string tok = s.substr(i, j - i);
        i = j + 1;
        if (tok == "#") return std::nullopt;
        return std::stoi(tok);
    };
    std::optional<int> first = next();
    if (!first) return nullptr;
    Node* root = arena.make(*first);
    std::deque<Node*> q{root};
    while (!q.empty()) {
        Node* p = q.front();
        q.pop_front();
        for (Node** slot : {&p->left, &p->right}) {
            std::optional<int> v = next();
            if (v) {
                *slot = arena.make(*v);
                q.push_back(*slot);
            }
        }
    }
    return root;
}
```

Both orders have the same number of tokens before trimming; level order is shorter for trees that are full at the bottom (section 5.6), and it is the format of most problem statements.

### 5.5 Compact binary

Text spends a byte on every comma and `#`, and several on every number. The binary form below uses the structure of section 5.1 directly: one **bit** per slot (1 = a node is here, 0 = missing), in preorder, then all the values, so a tree of `n` nodes needs `2n + 1` bits for its shape. Values are stored as **varints**: 7 bits per byte, the high bit says that another byte follows, so small numbers take one byte. A negative number would take five bytes, so the values are **zigzag** encoded first: `0, -1, 1, -2, ...` become `0, 1, 2, 3, ...`.

```cpp
inline void putVarint(std::string& out, uint32_t v) {            // 7 bits per byte, high bit = more
    while (v >= 0x80) {
        out += (char)(v | 0x80);
        v >>= 7;
    }
    out += (char)v;
}
inline uint32_t getVarint(const std::string& s, size_t& i) {
    uint32_t v = 0;
    for (int shift = 0;; shift += 7) {
        uint8_t b = (uint8_t)s.at(i++);
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return v;
    }
}
inline uint32_t zigzag(int v) { return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); }   // small |v| stays small
inline int unzigzag(uint32_t u) { return (int)(u >> 1) ^ -(int)(u & 1); }

inline std::string encode(const Node* root) {    // [count][structure bits][values], all in preorder
    std::vector<bool> bits;
    Ints vals;
    std::vector<const Node*> st{root};
    while (!st.empty()) {
        const Node* n = st.back();
        st.pop_back();
        bits.push_back(n != nullptr);
        if (!n) continue;
        vals.push_back(n->val);
        st.push_back(n->right);
        st.push_back(n->left);
    }
    std::string out;
    putVarint(out, (uint32_t)vals.size());
    std::string packed((bits.size() + 7) / 8, '\0');
    for (size_t k = 0; k < bits.size(); ++k)
        if (bits[k]) packed[k / 8] |= (char)(1 << (k % 8));
    out += packed;
    for (int v : vals) putVarint(out, zigzag(v));
    return out;
}

inline Node* decode(Arena& arena, const std::string& s) {
    size_t i = 0;
    uint32_t n = getVarint(s, i);
    size_t bitsAt = i;
    size_t valuesAt = bitsAt + (2 * (size_t)n + 1 + 7) / 8;
    size_t k = 0;                                // index of the next structure bit
    return rebuild(arena, [&]() -> std::optional<int> {
        bool present = (s.at(bitsAt + k / 8) >> (k % 8)) & 1;
        ++k;
        if (!present) return std::nullopt;
        return unzigzag(getVarint(s, valuesAt));
    });
}
```

The layout is `[varint n][ceil((2n + 1) / 8) bytes of structure][n varints of values]`. The decoder reuses `rebuild`: its `Next` reads one structure bit and, if it is 1, one value.

### 5.6 Sizes and costs

Measured sizes in bytes (exact):

| Tree | Preorder text | Level-order text | Binary |
|---|---|---|---|
| Random shape, 1000 nodes, values 0 to 99 | 4889 | 4883 | 1609 |
| Random shape, 1000 nodes, values 0 to 999,999 | 8912 | 8906 | 3250 |
| Random shape, 1000 nodes, values within 500,000 of zero | 9253 | 9247 | 3234 |
| Perfect, 1023 nodes, values 0 to 1022 | 6053 | 4005 | 2240 |

The binary form was 2.7 to 3.0 times smaller than the text for the random trees. For the perfect tree, level order wins over preorder text because the 1024 trailing markers (2048 bytes) are dropped. On one million random nodes: encoding took 80.5 ms and decoding 20.8 ms for 3,241,748 bytes, against 8,888,892 bytes of text.

A **binary search tree** needs no markers at all. If the values are written in preorder, inserting them one by one in that order into an empty BST gives back the same shape (the BST order tells where each value goes). So `n` numbers are enough; this is LeetCode 449 and comes back in tutorial 08.


---

## 6. Is this the same subtree?

Questions like "is `sub` a subtree of `root`" and "which subtrees occur twice" ask for equality of subtrees at every node. Comparing two subtrees node by node is `O(size)`, and doing it for every pair of nodes is quadratic. Both methods below reduce the question to comparing numbers or strings.

### 6.1 With strings

If the serialization of a subtree is a *contiguous part* of the serialization of the whole tree, `sub` is a subtree of `root` when its string occurs inside the string of `root`. This is true for the preorder format with markers and commas, and only if the match starts at a token. Three mistakes make it wrong, each one tested with a failing example:

| Mistake | Failing example | Why |
|---|---|---|
| No separators between numbers | `root` is one node 12, `sub` is one node 2: `12##` contains `2##` | The match starts in the middle of a number |
| No leading separator, so the match need not start at a token | the same trees with commas: `12,#,#,` contains `2,#,#,` | The match still starts in the middle of `12` |
| No markers for missing children | `root` = 1 with left child 2 with left child 3; `sub` = 2 with right child 3: `1,2,3,` contains `2,3,` | Different shapes, same values |

```cpp
inline bool isSubtreeText(const Node* root, const Node* sub) {
    std::string a = "," + serialize(root);       // a leading comma: a match must start at a token
    std::string b = "," + serialize(sub);
    return a.find(b) != std::string::npos;
}
```

The leading comma on both strings forces the match to begin at a token. The search is `O(n * m)` in the worst case for a plain `find`; use a linear-time string search if the strings are long. A match that begins at a token and is a complete serialization must be the whole subtree at that place, so there are no false positives; the tests agree with a brute force on 600 random trees.

### 6.2 With canonical ids

Strings can be avoided. Give each subtree a small integer, its **id**, so that two subtrees have the same id exactly when they are equal. The id of a node is decided by a triple: the id of its left subtree, its value and the id of its right subtree. Equal triples mean equal subtrees, by induction from the leaves, and the id 0 stands for a missing child. A map from triple to id does the rest, in one postorder pass.

```cpp
using IdMap = std::map<std::tuple<int, int, int>, int>;    // (left id, value, right id) -> id

inline int subtreeId(const Node* n, IdMap& ids, std::vector<std::pair<const Node*, int>>& out) {
    if (!n) return 0;                            // 0 is the id of "no subtree"
    int l = subtreeId(n->left, ids, out);
    int r = subtreeId(n->right, ids, out);
    auto key = std::make_tuple(l, n->val, r);
    auto it = ids.try_emplace(key, (int)ids.size() + 1).first;   // a new id if the key is new
    int id = it->second;
    out.push_back({n, id});                      // two subtrees are equal exactly when their ids are
    return id;
}

inline std::vector<const Node*> duplicateSubtrees(const Node* root) {   // one root per repeated shape
    IdMap ids;
    std::vector<std::pair<const Node*, int>> all;
    subtreeId(root, ids, all);
    std::map<int, int> count;
    std::vector<const Node*> dup;
    for (auto& [node, id] : all)
        if (++count[id] == 2) dup.push_back(node);
    return dup;
}

inline bool isSubtree(const Node* root, const Node* sub) {
    IdMap ids;
    std::vector<std::pair<const Node*, int>> all, tmp;
    int want = subtreeId(sub, ids, tmp);
    subtreeId(root, ids, all);                   // the same map, so ids are comparable
    for (auto& p : all)
        if (p.second == want) return true;
    return false;
}
```

- **Subtree of Another Tree (572):** put `sub` and `root` in the same map, and `sub` is a subtree if some node of `root` has the id of `sub`.
- **Find Duplicate Subtrees (652):** count the ids; report one node for each id that occurs twice or more.

Each node costs one map lookup, so the time is `O(n log n)` with `std::map` (a hash map would make it `O(n)` expected). It is exact: no collisions, unlike a hash of the subtree, where two different subtrees can get the same 64-bit value with a small probability. The recursion is as deep as the tree; for a very deep tree use the postorder of tutorial 02 section 3.

---

## 7. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Building the left subtree first from the back of a postorder | The wrong subtree gets each part | Build the right one first (section 2) |
| Using `m` instead of `m + 1` for the right range | The root is counted twice and the builder never ends or reads past the array | The right range starts after the root |
| Equal values with pre plus in | Several trees fit; the map keeps only one position | Distinct values, or a different method |
| Expecting pre plus post to give one tree | One child can be on either side | Only full trees are determined (section 1.3) |
| Serializing without null markers | 1 2 3 fits many trees | One marker per null link |
| Substring search without a leading separator or without markers | False positives (section 6.1) | Use ids, or the two fixes |
| Recursive serializer on a deep tree | Stack overflow | An explicit stack (section 5.2) |
| Negative numbers as plain varints | Five bytes each | Zigzag first |

---

## 8. Questions to answer out loud

1. Which pairs of traversals identify a binary tree, and what must be true of the values?
2. Give two trees with the same preorder and the same postorder.
3. Walk through the build from preorder 3 9 20 15 7 and inorder 9 3 15 20 7.
4. Why is the left subtree built first in the preorder version and the right one first in the postorder version?
5. What does the position map buy, and what does it cost?
6. Explain the stack builder: when is a node the left child of the top, and when does it close nodes?
7. Why does a null marker per missing child make preorder enough, and how does the reader know where the tree ends?
8. Why is a valid string one `#` longer than the number of numbers, and how does the slot counter use that?
9. How many bits does the shape of an `n`-node tree take in the binary form, and why does zigzag help?
10. Name the three mistakes that break the substring test, and the method that avoids strings.

---

## 9. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 9.1 On paper

**Problem A.** Build the tree with preorder 3 9 20 15 7 and inorder 9 3 15 20 7, and give its postorder.

*Where:* paper. *Goal:* the tree and one list. *How:* the root is 3; its position in the inorder splits 9 from 15 20 7.

**Problem B.** Build the same tree from postorder 9 15 7 20 3 and inorder 9 3 15 20 7.

*Where:* paper. *Goal:* the same tree. *How:* read the postorder from the back: 3, then the right subtree.

**Problem C.** Give two different trees with three nodes that have the same preorder and postorder.

*Where:* paper. *Goal:* two drawings and their sequences. *How:* the child of a one-child node can be on either side.

**Problem D.** Run `buildPreInIter` on preorder 1 2 4 5 3 and inorder 4 2 5 1 3. Write the stack after each step.

*Where:* paper. *Goal:* a table like section 3. *How:* compare the top of the stack with the next inorder value.

**Problem E.** Write the preorder text and the level-order text of the tree of Problem A. Then count the slots of `validPreorder` for `3,9,#,#,20,15,#,#,7,#,#,`.

*Where:* paper. *Goal:* two strings and a list of slot counts. *How:* start at 1; subtract 1 per token, add 2 per number.

**Problem F.** Give an id to every node of the tree 1 (left 2 (left 4), right 3 (left 2 (left 4), right 4)) and list the duplicate subtrees.

*Where:* paper. *Goal:* the ids and two duplicates. *How:* the id of a node is decided by (left id, value, right id); a missing child has id 0.

### 9.2 In your editor

For each, write the straightforward version first, then compare it with a brute force on all small trees.

1. **Construct Binary Tree from Preorder and Inorder Traversal (105).** Where: LeetCode. Goal: the tree. How: the map version; then the stack version, and compare them on a chain of 100,000 nodes.
2. **Construct Binary Tree from Inorder and Postorder Traversal (106).** Where: LeetCode. Goal: the tree. How: the mirror image; build the right subtree first.
3. **Construct Binary Tree from Preorder and Postorder Traversal (889).** Where: LeetCode. Goal: any tree that fits. How: the left root's position in the postorder gives the size of the left part.
4. **Serialize and Deserialize Binary Tree (297).** Where: LeetCode. Goal: both functions, for any format. How: preorder text, then level order; test with negative values and an empty tree.
5. **Serialize and Deserialize BST (449).** Where: LeetCode. Goal: the shortest encoding. How: the preorder of the values, with no markers; rebuild by insertion.
6. **Verify Preorder Serialization of a Binary Tree (331).** Where: LeetCode. Goal: true or false, without building the tree. How: the slot counter.
7. **Subtree of Another Tree (572).** Where: LeetCode. Goal: true or false. How: the id method, then the string method; find an input where each naive string variant fails.
8. **Find Duplicate Subtrees (652).** Where: LeetCode. Goal: one root for every duplicated shape. How: the ids and a count.

---

## Answers

**Problem A.** Root 3; the inorder splits into 9 and 15 20 7. The left subtree is the node 9. In the right part the next preorder value is 20, which splits 15 from 7. So 3 has the left child 9 and the right child 20, which has the children 15 and 7. Its postorder is 9 15 7 20 3.

**Problem B.** The last postorder value is 3, so the root is 3 and the inorder splits as in Problem A. Reading the postorder from the back: 20 is the root of the right part, then 7 is its right child, then 15 its left child, then 9 is the left child of 3. The tree is the same.

**Problem C.** A root 1 with only a left child 2, and a root 1 with only a right child 2: both have preorder 1 2 and postorder 2 1.

**Problem D.** The tree is 1 with the left child 2 (children 4 and 5) and the right child 3. Steps (stack bottom to top): next value 2, stack `1`, top is not 4, `1.left = 2`, stack `1 2`. Value 4: top 2 is not 4, `2.left = 4`, stack `1 2 4`. Value 5: top 4 equals 4, close 4 (next inorder 2), close 2 (next 5); `2.right = 5`; stack `1 5`. Value 3: top 5 equals 5, close 5 (next 1), close 1 (next 3); `1.right = 3`; stack `3`. These steps come from the Python model, and the C++ code builds the same tree.

**Problem E.** Preorder text: `3,9,#,#,20,15,#,#,7,#,#,`. Level order: `3,9,20,#,#,15,7,` (after the trailing markers are dropped). Slots for the string: start 1; after `3` it is 2; `9` 3; `#` 2; `#` 1; `20` 2; `15` 3; `#` 2; `#` 1; `7` 2; `#` 1; `#` 0 at the last token, so the string is valid.

**Problem F.** The leaf 4 gets the same id wherever it is (triple `(0, 4, 0)`). The subtree 2 with the left child 4 gets one id, and it occurs twice. The leaf 4 occurs three times. So the duplicates are the leaf 4 and the subtree 2(4): two shapes. (The model and the C++ code agree.)

**Problems 9.2.** All eight follow the listings. For 449 the preorder of the values is enough because a BST is rebuilt by inserting them in that order. For 331 the slot counter must be checked at every token, not only at the end. For 572 and 652 the ids are exact; the string method needs the leading separator and the markers.

---

## Cheat sheet

- Preorder or postorder plus inorder identify a tree if the values are distinct. Preorder plus postorder do not (a lone child can be on either side), except for full trees.
- Build: the root is the first preorder value (the last postorder value); its inorder position splits the rest. Preorder: left then right. Postorder from the back: right then left. With a position map, `O(n)`.
- The stack builder: the next preorder value is the left child of the top unless the top equals the next inorder value; then close nodes until it does not, and attach as a right child. `O(n)`, no map, no recursion.
- Serialization: preorder with one `#` per null link, and a separator after every token. The reader keeps a stack of open nodes with a child counter; it stops when the stack is empty.
- A valid string: slots start at 1, minus 1 per token, plus 2 per number, 0 at the end and never before.
- Level order: queue; trim the trailing markers. Binary: `2n + 1` bits of shape, then zigzag varints; about 3 times smaller than text here. A BST needs only the preorder of its values.
- Subtree equality: strings need separators, a leading separator and markers. Ids from `(left id, value, right id)` are exact, `O(n log n)`.

---

## Sources

- Titles and numbers of 105, 106 and 331 were checked against the [talentd.in tree problem list](https://www.talentd.in/fleetcode/problems?topic=tree), and 889, 297 and 449 against the [tree problem table of a4lamber's notes](https://a4lamber.github.io/Leetcode/data_structure/tree/problems). The titles of 572 and 652 were seen in search results ([doocs, 572](https://leetcode.doocs.org/en/lc/572/) and [doocs, 652](https://leetcode.doocs.org/en/lc/652/)). Difficulty labels are not used because I could not verify them.
- The Motzkin and Catalan identifications in section 1.3 rest on the computed sequences (the Motzkin recurrence was checked in the verifier); I did not prove them.
- All other claims in this tutorial come from the tests and measurements described at the top.
