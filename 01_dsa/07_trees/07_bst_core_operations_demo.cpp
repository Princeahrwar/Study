// DSA 07.07 companion: the BST of the tutorial, with a small demo in main().
// Build: g++ -std=c++17 -Wall -Wextra -O2 07_bst_core_operations_demo.cpp -o bst_demo
#include <climits>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

struct Node {
    int key;
    Node* left = nullptr;
    Node* right = nullptr;
};

inline std::vector<int> inorder(const Node* root) {            // keys in increasing order
    std::vector<int> out;
    std::vector<const Node*> st;
    const Node* cur = root;
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        out.push_back(cur->key);
        cur = cur->right;
    }
    return out;
}

inline int height(const Node* root) {                          // number of levels, by the level loop of Topic 05
    int h = 0;
    std::vector<const Node*> cur, nxt;
    if (root) cur.push_back(root);
    while (!cur.empty()) {
        ++h;
        nxt.clear();
        for (const Node* n : cur) {
            if (n->left) nxt.push_back(n->left);
            if (n->right) nxt.push_back(n->right);
        }
        std::swap(cur, nxt);
    }
    return h;
}

inline void destroy(Node*& root) {                             // iterative: a degenerate tree must not overflow the stack
    std::vector<Node*> st;
    if (root) st.push_back(root);
    while (!st.empty()) {
        Node* n = st.back();
        st.pop_back();
        if (n->left) st.push_back(n->left);
        if (n->right) st.push_back(n->right);
        delete n;
    }
    root = nullptr;
}

// Search: at each node the key tells you which half cannot contain the answer.
inline const Node* find(const Node* root, int key) {
    const Node* n = root;
    while (n && n->key != key) n = key < n->key ? n->left : n->right;
    return n;                                                  // null if the key is absent
}

inline bool contains(const Node* root, int key) { return find(root, key) != nullptr; }

inline const Node* minNode(const Node* n) {                    // the leftmost node (n must not be null)
    while (n->left) n = n->left;
    return n;
}

inline const Node* maxNode(const Node* n) {                    // the rightmost node (n must not be null)
    while (n->right) n = n->right;
    return n;
}

// Insert without recursion and without a parent pointer: `link` always points at the pointer
// that would have to change, so the new node is attached by one assignment.
inline bool insert(Node*& root, int key) {                     // false if the key is already there
    Node** link = &root;
    while (*link) {
        if (key == (*link)->key) return false;                 // this tree keeps keys unique
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    *link = new Node{key};
    return true;
}

// Erase by relinking: no key is copied, so every other node keeps its address.
inline bool erase(Node*& root, int key) {
    Node** link = &root;                                       // the pointer that points at the node to remove
    while (*link && (*link)->key != key) link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node* n = *link;
    if (!n) return false;
    if (!n->left) {
        *link = n->right;                                      // case 1 and 2: no left child, splice the right one in
    } else if (!n->right) {
        *link = n->left;                                       // case 2: no right child, splice the left one in
    } else {                                                   // case 3: two children
        Node** sl = &n->right;                                 // link to the successor: leftmost of the right subtree
        while ((*sl)->left) sl = &(*sl)->left;
        Node* s = *sl;
        *sl = s->right;                                        // detach it (it has no left child)
        s->left = n->left;                                     // and put it where n was
        s->right = n->right;
        *link = s;
    }
    delete n;
    return true;
}

// The textbook variant: copy the successor's key into n and delete the successor's node instead.
// Shorter, but the key moves between nodes, so a pointer to the successor's node is invalidated.
inline bool eraseCopy(Node*& root, int key) {
    Node** link = &root;
    while (*link && (*link)->key != key) link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node* n = *link;
    if (!n) return false;
    if (n->left && n->right) {
        Node** sl = &n->right;
        while ((*sl)->left) sl = &(*sl)->left;
        Node* s = *sl;
        n->key = s->key;                                       // the copy
        *sl = s->right;
        delete s;
        return true;
    }
    *link = n->left ? n->left : n->right;
    delete n;
    return true;
}

// Neighbours of a KEY, found from the root with no parent pointers and no stack.
// Each walk remembers the last node where it turned the "right" way.
inline std::optional<int> floorKey(const Node* n, int key) {   // greatest key <= key
    std::optional<int> best;
    while (n) {
        if (n->key == key) return key;
        if (n->key < key) { best = n->key; n = n->right; }     // n is a candidate; look for a closer one on the right
        else n = n->left;
    }
    return best;
}

inline std::optional<int> ceilKey(const Node* n, int key) {    // smallest key >= key
    std::optional<int> best;
    while (n) {
        if (n->key == key) return key;
        if (n->key > key) { best = n->key; n = n->left; }
        else n = n->right;
    }
    return best;
}

inline std::optional<int> predecessorKey(const Node* n, int key) {   // greatest key < key
    std::optional<int> best;
    while (n) {
        if (n->key < key) { best = n->key; n = n->right; }
        else n = n->left;                                      // n->key >= key: nothing here or on its right is smaller
    }
    return best;
}

inline std::optional<int> successorKey(const Node* n, int key) {     // smallest key > key
    std::optional<int> best;
    while (n) {
        if (n->key > key) { best = n->key; n = n->left; }
        else n = n->right;
    }
    return best;
}

// Correct: an inorder walk of a BST is strictly increasing, so check that, with no sentinel value.
inline bool isBst(const Node* root) {
    std::vector<const Node*> st;
    const Node* cur = root;
    std::optional<int> prev;                                   // empty until the first key is seen
    while (cur || !st.empty()) {
        while (cur) { st.push_back(cur); cur = cur->left; }
        cur = st.back();
        st.pop_back();
        if (prev && cur->key <= *prev) return false;          // <= : equal keys are not allowed here
        prev = cur->key;
        cur = cur->right;
    }
    return true;
}

// A small owner around the functions above, so a tree cannot leak or be copied by accident.
class Bst {
public:
    Bst() = default;
    Bst(const Bst&) = delete;
    Bst& operator=(const Bst&) = delete;
    ~Bst() { destroy(root_); }

    bool insert(int key) { bool added = ::insert(root_, key); size_ += added; return added; }
    bool erase(int key) { bool gone = ::erase(root_, key); size_ -= gone; return gone; }
    bool contains(int key) const { return ::contains(root_, key); }
    std::optional<int> floor(int key) const { return floorKey(root_, key); }
    std::optional<int> ceil(int key) const { return ceilKey(root_, key); }
    std::optional<int> lower(int key) const { return predecessorKey(root_, key); }
    std::optional<int> higher(int key) const { return successorKey(root_, key); }
    std::optional<int> min() const { return root_ ? std::optional<int>(minNode(root_)->key) : std::nullopt; }
    std::optional<int> max() const { return root_ ? std::optional<int>(maxNode(root_)->key) : std::nullopt; }
    size_t size() const { return size_; }
    const Node* root() const { return root_; }

private:
    Node* root_ = nullptr;
    size_t size_ = 0;
};

static void print(const char* label, std::optional<int> v) {
    if (v) std::printf("%s %d\n", label, *v);
    else std::printf("%s none\n", label);
}

int main() {
    Bst t;
    for (int k : {8, 3, 10, 1, 6, 14, 4, 7, 13}) t.insert(k);
    std::printf("size %zu, height %d, inorder:", t.size(), height(t.root()));
    for (int k : inorder(t.root())) std::printf(" %d", k);
    std::printf("\n");
    print("floor(5)  =", t.floor(5));
    print("ceil(5)   =", t.ceil(5));
    print("lower(8)  =", t.lower(8));
    print("higher(8) =", t.higher(8));
    print("floor(0)  =", t.floor(0));
    const bool first = t.insert(5);
    const bool second = t.insert(5);
    std::printf("insert(5): %s, insert(5) again: %s\n", first ? "added" : "refused", second ? "added" : "refused");
    for (int k : {1, 10, 3, 8, 42}) std::printf("erase(%d): %s\n", k, t.erase(k) ? "removed" : "absent");
    std::printf("after the erases: size %zu, valid BST %s, inorder:", t.size(), isBst(t.root()) ? "yes" : "no");
    for (int k : inorder(t.root())) std::printf(" %d", k);
    std::printf("\n");

    // sorted input makes a chain: the height equals the number of keys
    Bst chain;
    for (int k = 1; k <= 2000; ++k) chain.insert(k);
    std::printf("2000 sorted keys: height %d\n", height(chain.root()));
}
