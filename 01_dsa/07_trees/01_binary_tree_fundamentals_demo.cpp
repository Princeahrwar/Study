// Topic 01 companion: binary tree fundamentals.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined 01_binary_tree_fundamentals_demo.cpp -o demo && ./demo
//
// Every function shown in 01_binary_tree_fundamentals.md is defined here exactly as written there.
// main() prints the demo trees, then runs the checks at the bottom of the file.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// Node
// ============================================================================

template <typename T>
struct TreeNode {
    T val;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;

    explicit TreeNode(const T& v) : val(v) {}
    explicit TreeNode(T&& v) : val(std::move(v)) {}
};

// ============================================================================
// Ownership: destroy and clone without recursion
// ============================================================================

// Frees every node in O(n) time and O(1) extra space, with no recursion.
// While the current node has a left child, rotate right so that child moves up.
// Once there is no left child, delete the node and continue with its right subtree.
template <typename T>
void destroyTree(TreeNode<T>* node) noexcept {
    while (node != nullptr) {
        if (node->left != nullptr) {
            TreeNode<T>* l = node->left;
            node->left = l->right;   // l's right subtree becomes node's left subtree
            l->right = node;         // node hangs off l's right
            node = l;                // l is the new top
        } else {
            TreeNode<T>* next = node->right;
            delete node;
            node = next;
        }
    }
}

// Deep copy without recursion. Every new node is linked into the copy the moment
// it is created, so if anything throws, destroyTree(copy) frees exactly what was built.
template <typename T>
TreeNode<T>* cloneTree(const TreeNode<T>* src) {
    if (src == nullptr) return nullptr;
    TreeNode<T>* copy = new TreeNode<T>(src->val);
    try {
        std::vector<std::pair<const TreeNode<T>*, TreeNode<T>*>> stack;
        stack.emplace_back(src, copy);
        while (!stack.empty()) {
            auto [s, d] = stack.back();
            stack.pop_back();
            if (s->left != nullptr) {
                d->left = new TreeNode<T>(s->left->val);
                stack.emplace_back(s->left, d->left);
            }
            if (s->right != nullptr) {
                d->right = new TreeNode<T>(s->right->val);
                stack.emplace_back(s->right, d->right);
            }
        }
    } catch (...) {
        destroyTree(copy);
        throw;
    }
    return copy;
}

// ============================================================================
// Owning wrapper: Rule of 5
// ============================================================================

template <typename T>
class BinaryTree {
public:
    using Node = TreeNode<T>;

    BinaryTree() noexcept = default;
    ~BinaryTree() { destroyTree(root_); }

    BinaryTree(const BinaryTree& other) : root_(cloneTree(other.root_)) {}

    BinaryTree& operator=(const BinaryTree& other) {
        if (this != &other) {
            BinaryTree tmp(other);   // may throw: *this is untouched if it does
            swap(tmp);               // cannot throw
        }                            // tmp's destructor frees our old nodes
        return *this;
    }

    BinaryTree(BinaryTree&& other) noexcept : root_(std::exchange(other.root_, nullptr)) {}

    BinaryTree& operator=(BinaryTree&& other) noexcept {
        if (this != &other) {
            destroyTree(root_);
            root_ = std::exchange(other.root_, nullptr);
        }
        return *this;
    }

    void swap(BinaryTree& other) noexcept { std::swap(root_, other.root_); }

    // LeetCode-style level order with gaps, e.g. {1, 2, 3, nullopt, 4}.
    static BinaryTree fromLevelOrder(const std::vector<std::optional<T>>& values) {
        BinaryTree tree;
        if (values.empty() || !values[0].has_value()) return tree;

        tree.root_ = new Node(*values[0]);   // owned by `tree` from here on
        std::queue<Node*> parents;
        parents.push(tree.root_);
        std::size_t i = 1;
        while (i < values.size() && !parents.empty()) {
            Node* parent = parents.front();
            parents.pop();
            if (values[i].has_value()) {
                parent->left = new Node(*values[i]);
                parents.push(parent->left);
            }
            ++i;
            if (i < values.size() && values[i].has_value()) {
                parent->right = new Node(*values[i]);
                parents.push(parent->right);
            }
            ++i;
        }
        for (; i < values.size(); ++i) {
            if (values[i].has_value())
                throw std::invalid_argument("fromLevelOrder: value has no parent slot");
        }
        return tree;
    }

    std::vector<std::optional<T>> toLevelOrder() const {
        std::vector<std::optional<T>> out;
        if (root_ == nullptr) return out;
        std::queue<const Node*> q;
        q.push(root_);
        while (!q.empty()) {
            const Node* node = q.front();
            q.pop();
            if (node == nullptr) {
                out.emplace_back(std::nullopt);
                continue;
            }
            out.emplace_back(node->val);
            q.push(node->left);    // nulls are queued too: they become the gaps
            q.push(node->right);
        }
        while (!out.empty() && !out.back().has_value()) out.pop_back();   // trim trailing gaps
        return out;
    }

    const Node* root() const noexcept { return root_; }
    Node* root() noexcept { return root_; }
    bool empty() const noexcept { return root_ == nullptr; }

private:
    Node* root_ = nullptr;
};

// ============================================================================
// Depth-first traversals (recursive)
// ============================================================================

template <typename T>
void preorder(const TreeNode<T>* node, std::vector<T>& out) {
    if (node == nullptr) return;
    out.push_back(node->val);       // visit
    preorder(node->left, out);
    preorder(node->right, out);
}

template <typename T>
void inorder(const TreeNode<T>* node, std::vector<T>& out) {
    if (node == nullptr) return;
    inorder(node->left, out);
    out.push_back(node->val);       // visit
    inorder(node->right, out);
}

template <typename T>
void postorder(const TreeNode<T>* node, std::vector<T>& out) {
    if (node == nullptr) return;
    postorder(node->left, out);
    postorder(node->right, out);
    out.push_back(node->val);       // visit
}

enum class Order { Pre, In, Post };

// One traversal, three visit positions.
template <typename T, typename Visit>
void dfs(const TreeNode<T>* node, Order order, Visit&& visit) {
    if (node == nullptr) return;
    if (order == Order::Pre) visit(node->val);
    dfs(node->left, order, visit);
    if (order == Order::In) visit(node->val);
    dfs(node->right, order, visit);
    if (order == Order::Post) visit(node->val);
}

// ============================================================================
// Breadth-first traversal
// ============================================================================

template <typename T>
std::vector<std::vector<T>> levelOrder(const TreeNode<T>* root) {
    std::vector<std::vector<T>> levels;
    if (root == nullptr) return levels;

    std::queue<const TreeNode<T>*> q;
    q.push(root);
    while (!q.empty()) {
        const std::size_t width = q.size();   // snapshot: exactly the nodes of this level
        std::vector<T> level;
        level.reserve(width);
        for (std::size_t i = 0; i < width; ++i) {
            const TreeNode<T>* node = q.front();
            q.pop();
            level.push_back(node->val);
            if (node->left != nullptr) q.push(node->left);
            if (node->right != nullptr) q.push(node->right);
        }
        levels.push_back(std::move(level));
    }
    return levels;
}

// The bug from the pitfalls section: q.size() re-read on every iteration.
template <typename T>
std::vector<std::vector<T>> levelOrderBuggy(const TreeNode<T>* root) {
    std::vector<std::vector<T>> levels;
    if (root == nullptr) return levels;
    std::queue<const TreeNode<T>*> q;
    q.push(root);
    while (!q.empty()) {
        std::vector<T> level;
        for (std::size_t i = 0; i < q.size(); ++i) {   // BUG: size changes as we push
            const TreeNode<T>* node = q.front();
            q.pop();
            level.push_back(node->val);
            if (node->left != nullptr) q.push(node->left);
            if (node->right != nullptr) q.push(node->right);
        }
        levels.push_back(std::move(level));
    }
    return levels;
}

// ============================================================================
// Basic recursive metrics: f(node) = combine(f(left), f(right))
// ============================================================================

template <typename T>
std::size_t countNodes(const TreeNode<T>* node) {
    if (node == nullptr) return 0;
    return 1 + countNodes(node->left) + countNodes(node->right);
}

// Height in EDGES: empty tree = -1, single node = 0.
template <typename T>
int height(const TreeNode<T>* node) {
    if (node == nullptr) return -1;
    return 1 + std::max(height(node->left), height(node->right));
}

// LeetCode 104 "maximum depth" counts NODES on the longest root-to-leaf path: height + 1.
template <typename T>
int maxDepth(const TreeNode<T>* node) {
    if (node == nullptr) return 0;
    return 1 + std::max(maxDepth(node->left), maxDepth(node->right));
}

template <typename T>
std::size_t countLeaves(const TreeNode<T>* node) {
    if (node == nullptr) return 0;
    if (node->left == nullptr && node->right == nullptr) return 1;
    return countLeaves(node->left) + countLeaves(node->right);
}

template <typename T>
bool contains(const TreeNode<T>* node, const T& target) {
    if (node == nullptr) return false;
    return node->val == target || contains(node->left, target) || contains(node->right, target);
}

// LeetCode 111: number of nodes on the path to the SHALLOWEST LEAF.
template <typename T>
int minDepth(const TreeNode<T>* node) {
    if (node == nullptr) return 0;
    if (node->left == nullptr) return 1 + minDepth(node->right);   // not a leaf: must go right
    if (node->right == nullptr) return 1 + minDepth(node->left);   // not a leaf: must go left
    return 1 + std::min(minDepth(node->left), minDepth(node->right));
}

// The bug from the pitfalls section: a missing child is treated as a leaf at depth 0.
template <typename T>
int minDepthBuggy(const TreeNode<T>* node) {
    if (node == nullptr) return 0;
    return 1 + std::min(minDepthBuggy(node->left), minDepthBuggy(node->right));
}

// ============================================================================
// Visualization: reverse inorder, root on the left, right subtree on top
// ============================================================================

template <typename T>
void printSideways(const TreeNode<T>* node, std::ostream& os, int depth = 0) {
    if (node == nullptr) return;
    printSideways(node->right, os, depth + 1);
    os << std::string(static_cast<std::size_t>(depth) * 4, ' ') << node->val << '\n';
    printSideways(node->left, os, depth + 1);
}

// ============================================================================
// Expression trees: postorder evaluation
// ============================================================================

// Leaves hold integers; internal nodes hold one of + - * /.
long long evaluate(const TreeNode<std::string>* node) {
    if (node == nullptr) throw std::invalid_argument("evaluate: empty expression");
    if (node->left == nullptr && node->right == nullptr) return std::stoll(node->val);
    if (node->left == nullptr || node->right == nullptr)
        throw std::invalid_argument("evaluate: operator needs two operands");

    const long long a = evaluate(node->left);    // left subtree first,
    const long long b = evaluate(node->right);   // then right subtree,
    const std::string& op = node->val;           // then this node: postorder
    if (op == "+") return a + b;
    if (op == "-") return a - b;
    if (op == "*") return a * b;
    if (op == "/") {
        if (b == 0) throw std::domain_error("evaluate: division by zero");
        return a / b;
    }
    throw std::invalid_argument("evaluate: unknown operator " + op);
}

// ============================================================================
// Other representations
// ============================================================================

// Implicit (array) layout, 0-indexed: the shape is in the index arithmetic, not in pointers.
constexpr std::size_t leftChild(std::size_t i) { return 2 * i + 1; }
constexpr std::size_t rightChild(std::size_t i) { return 2 * i + 2; }
constexpr std::size_t parentOf(std::size_t i) { return (i - 1) / 2; }   // only for i > 0

// Index pool: all nodes in one vector, children are 32-bit indices, -1 means "no child".
struct IndexTree {
    static constexpr std::int32_t kNull = -1;
    struct Node {
        int val;
        std::int32_t left = kNull;
        std::int32_t right = kNull;
    };
    std::vector<Node> nodes;   // nodes[0] is the root when the tree is non-empty

    std::int32_t add(int val) {
        nodes.push_back(Node{val});
        return static_cast<std::int32_t>(nodes.size() - 1);
    }
};

void inorderIndexed(const IndexTree& t, std::int32_t i, std::vector<int>& out) {
    if (i == IndexTree::kNull) return;
    inorderIndexed(t, t.nodes[i].left, out);
    out.push_back(t.nodes[i].val);
    inorderIndexed(t, t.nodes[i].right, out);
}

// ============================================================================
// General (N-ary) trees
// ============================================================================

template <typename T>
struct NaryNode {
    T val;
    std::vector<NaryNode*> children;   // left to right

    explicit NaryNode(const T& v) : val(v) {}
};

// Frees every node without recursion. Unlike destroyTree, this needs a stack:
// the rotation trick only works when every node has exactly two child slots.
// Null entries are skipped, so a partly built tree can be freed too.
template <typename T>
void destroyNary(NaryNode<T>* root) {
    std::vector<NaryNode<T>*> stack;
    if (root != nullptr) stack.push_back(root);
    while (!stack.empty()) {
        NaryNode<T>* node = stack.back();
        stack.pop_back();
        for (NaryNode<T>* child : node->children)
            if (child != nullptr) stack.push_back(child);
        delete node;
    }
}

template <typename T>
void naryPreorder(const NaryNode<T>* node, std::vector<T>& out) {
    if (node == nullptr) return;
    out.push_back(node->val);                                     // visit first
    for (const NaryNode<T>* child : node->children) naryPreorder(child, out);
}

template <typename T>
void naryPostorder(const NaryNode<T>* node, std::vector<T>& out) {
    if (node == nullptr) return;
    for (const NaryNode<T>* child : node->children) naryPostorder(child, out);
    out.push_back(node->val);                                     // visit last
}

template <typename T>
std::vector<std::vector<T>> naryLevelOrder(const NaryNode<T>* root) {
    std::vector<std::vector<T>> levels;
    if (root == nullptr) return levels;

    std::queue<const NaryNode<T>*> q;
    q.push(root);
    while (!q.empty()) {
        const std::size_t width = q.size();   // same snapshot as the binary version
        std::vector<T> level;
        level.reserve(width);
        for (std::size_t i = 0; i < width; ++i) {
            const NaryNode<T>* node = q.front();
            q.pop();
            level.push_back(node->val);
            for (const NaryNode<T>* child : node->children) q.push(child);
        }
        levels.push_back(std::move(level));
    }
    return levels;
}

// Height in EDGES, same convention as height(): empty tree = -1, single node = 0.
template <typename T>
int naryHeight(const NaryNode<T>* node) {
    if (node == nullptr) return -1;
    int tallest = -1;   // a leaf has no children, so it ends at -1 + 1 = 0
    for (const NaryNode<T>* child : node->children) tallest = std::max(tallest, naryHeight(child));
    return tallest + 1;
}

// N-ary -> binary: left = first child, right = next sibling. No recursion.
template <typename T>
TreeNode<T>* toLeftChildRightSibling(const NaryNode<T>* root) {
    if (root == nullptr) return nullptr;
    TreeNode<T>* out = new TreeNode<T>(root->val);
    try {
        std::vector<std::pair<const NaryNode<T>*, TreeNode<T>*>> stack;
        stack.emplace_back(root, out);
        while (!stack.empty()) {
            auto [src, dst] = stack.back();
            stack.pop_back();
            TreeNode<T>** link = &dst->left;    // the first child hangs off left
            for (const NaryNode<T>* child : src->children) {
                *link = new TreeNode<T>(child->val);
                stack.emplace_back(child, *link);
                link = &(*link)->right;         // each later child hangs off the previous one's right
            }
        }
    } catch (...) {
        destroyTree(out);
        throw;
    }
    return out;
}

// Binary (left-child, right-sibling) -> N-ary. No recursion.
template <typename T>
NaryNode<T>* fromLeftChildRightSibling(const TreeNode<T>* root) {
    if (root == nullptr) return nullptr;
    if (root->right != nullptr)   // the root of a tree has no siblings; this would be a forest
        throw std::invalid_argument("fromLeftChildRightSibling: root has a right sibling");
    NaryNode<T>* out = new NaryNode<T>(root->val);
    try {
        std::vector<std::pair<const TreeNode<T>*, NaryNode<T>*>> stack;
        stack.emplace_back(root, out);
        while (!stack.empty()) {
            auto [src, dst] = stack.back();
            stack.pop_back();
            for (const TreeNode<T>* c = src->left; c != nullptr; c = c->right) {
                dst->children.push_back(nullptr);            // grow first: if this throws, nothing leaks
                dst->children.back() = new NaryNode<T>(c->val);
                stack.emplace_back(c, dst->children.back());
            }
        }
    } catch (...) {
        destroyNary(out);
        throw;
    }
    return out;
}

// ============================================================================
// Test helpers
// ============================================================================

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL: " << what << '\n';
    }
}

constexpr auto null = std::nullopt;
using Opt = std::vector<std::optional<int>>;

template <typename T>
std::vector<T> collect(const TreeNode<T>* root, Order order) {
    std::vector<T> out;
    dfs(root, order, [&out](const T& v) { out.push_back(v); });
    return out;
}

// A value type whose copy constructor can be told to throw, and which counts live objects.
struct Bomb {
    static inline int live = 0;
    static inline int copiesUntilThrow = -1;   // -1 means never throw

    int id;
    explicit Bomb(int i) : id(i) { ++live; }
    Bomb(const Bomb& other) : id(other.id) {
        if (copiesUntilThrow == 0) throw std::runtime_error("Bomb copy failed");
        if (copiesUntilThrow > 0) --copiesUntilThrow;
        ++live;
    }
    Bomb(Bomb&& other) noexcept : id(other.id) { ++live; }
    Bomb& operator=(const Bomb&) = default;
    ~Bomb() { --live; }
    bool operator==(const Bomb& other) const { return id == other.id; }
};

// Builds the expression (8 - 3) * (2 + 4).
TreeNode<std::string>* buildExpression() {
    auto* root = new TreeNode<std::string>("*");
    root->left = new TreeNode<std::string>("-");
    root->left->left = new TreeNode<std::string>("8");
    root->left->right = new TreeNode<std::string>("3");
    root->right = new TreeNode<std::string>("+");
    root->right->left = new TreeNode<std::string>("2");
    root->right->right = new TreeNode<std::string>("4");
    return root;
}

Opt rightSkewed(int n) {   // 1 -> 2 -> ... -> n, every node a right child
    Opt v{1};
    for (int k = 2; k <= n; ++k) {
        v.push_back(null);
        v.push_back(k);
    }
    return v;
}

Opt leftSkewed(int n) {    // 1 -> 2 -> ... -> n, every node a left child
    Opt v{1};
    for (int k = 2; k <= n; ++k) {
        v.push_back(k);
        if (k < n) v.push_back(null);   // no trailing gap: keep the canonical form
    }
    return v;
}

// The N-ary example from Section 11: 1 has children 2, 3, 4; 2 has 5, 6; 4 has 7.
NaryNode<int>* buildNarySample() {
    auto* root = new NaryNode<int>(1);
    for (int v : {2, 3, 4}) root->children.push_back(new NaryNode<int>(v));
    for (int v : {5, 6}) root->children[0]->children.push_back(new NaryNode<int>(v));
    root->children[2]->children.push_back(new NaryNode<int>(7));
    return root;
}

template <typename T>
bool sameNary(const NaryNode<T>* a, const NaryNode<T>* b) {   // structural equality (test-only)
    if (a == nullptr || b == nullptr) return a == b;
    if (!(a->val == b->val) || a->children.size() != b->children.size()) return false;
    for (std::size_t i = 0; i < a->children.size(); ++i)
        if (!sameNary(a->children[i], b->children[i])) return false;
    return true;
}

}  // namespace

// ============================================================================
// main
// ============================================================================

int main() {
    // ---- demo output
    const BinaryTree<int> sample = BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, 5});
    std::cout << "Sample tree [1,2,3,4,5], printed sideways (root on the left):\n";
    printSideways(sample.root(), std::cout);

    TreeNode<std::string>* expr = buildExpression();
    std::cout << "\nExpression tree for (8 - 3) * (2 + 4), printed sideways:\n";
    printSideways(expr, std::cout);
    std::cout << "value = " << evaluate(expr) << "\n\n";

    std::cout << "sizeof(TreeNode<int>)        = " << sizeof(TreeNode<int>) << " bytes\n";
    std::cout << "sizeof(IndexTree::Node)      = " << sizeof(IndexTree::Node) << " bytes\n\n";

    // ---- traversals on the sample tree
    {
        const TreeNode<int>* r = sample.root();
        std::vector<int> pre, in, post;
        preorder(r, pre);
        inorder(r, in);
        postorder(r, post);
        check(pre == std::vector<int>{1, 2, 4, 5, 3}, "preorder");
        check(in == std::vector<int>{4, 2, 5, 1, 3}, "inorder");
        check(post == std::vector<int>{4, 5, 2, 3, 1}, "postorder");
        check(collect(r, Order::Pre) == pre, "dfs Pre matches preorder");
        check(collect(r, Order::In) == in, "dfs In matches inorder");
        check(collect(r, Order::Post) == post, "dfs Post matches postorder");

        const std::vector<std::vector<int>> levels{{1}, {2, 3}, {4, 5}};
        check(levelOrder(r) == levels, "levelOrder");
        const std::vector<std::vector<int>> buggy{{1, 2, 3}, {4}, {5}};
        check(levelOrderBuggy(r) == buggy, "levelOrderBuggy mixes levels exactly as described");

        std::vector<int> empty;
        preorder<int>(nullptr, empty);
        check(empty.empty(), "preorder of empty tree");
        check(levelOrder<int>(nullptr).empty(), "levelOrder of empty tree");
    }

    // ---- metrics
    {
        const TreeNode<int>* r = sample.root();
        check(countNodes(r) == 5, "countNodes");
        check(height(r) == 2, "height in edges");
        check(maxDepth(r) == 3, "maxDepth in nodes");
        check(countLeaves(r) == 3, "countLeaves");
        check(contains(r, 5) && !contains(r, 42), "contains");
        check(height<int>(nullptr) == -1 && maxDepth<int>(nullptr) == 0, "empty-tree conventions");

        // full tree property: leaves = internal + 1
        check(countLeaves(r) == (countNodes(r) - countLeaves(r)) + 1, "leaves = internal + 1 (full tree)");

        const BinaryTree<int> perfect =
            BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
        check(countNodes(perfect.root()) == 15 && height(perfect.root()) == 3, "perfect tree: 2^(h+1)-1 nodes");

        const BinaryTree<int> stick = BinaryTree<int>::fromLevelOrder({1, null, 2});
        check(minDepth(stick.root()) == 2, "minDepth on [1,null,2]");
        check(minDepthBuggy(stick.root()) == 1, "minDepthBuggy returns the wrong answer 1");
        check(minDepth(r) == 2, "minDepth on sample");

        const BinaryTree<int> lopsided = BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, null, null, null, 5});
        check(minDepth(lopsided.root()) == 2 && maxDepth(lopsided.root()) == 4, "minDepth vs maxDepth");
    }

    // ---- printSideways output
    {
        std::ostringstream os;
        printSideways(sample.root(), os);
        check(os.str() == "    3\n1\n        5\n    2\n        4\n", "printSideways layout");
    }

    // ---- expression tree
    {
        std::vector<std::string> pre, in, post;
        preorder(expr, pre);
        inorder(expr, in);
        postorder(expr, post);
        check(pre == std::vector<std::string>{"*", "-", "8", "3", "+", "2", "4"}, "expression preorder (prefix)");
        check(in == std::vector<std::string>{"8", "-", "3", "*", "2", "+", "4"}, "expression inorder (infix, no parens)");
        check(post == std::vector<std::string>{"8", "3", "-", "2", "4", "+", "*"}, "expression postorder (postfix)");
        check(evaluate(expr) == 30, "evaluate (8-3)*(2+4)");

        TreeNode<std::string>* bad = new TreeNode<std::string>("/");
        bad->left = new TreeNode<std::string>("1");
        bad->right = new TreeNode<std::string>("0");
        bool threw = false;
        try {
            evaluate(bad);
        } catch (const std::domain_error&) {
            threw = true;
        }
        check(threw, "evaluate rejects division by zero");
        destroyTree(bad);
        destroyTree(expr);
    }

    // ---- level-order round trips
    {
        const std::vector<Opt> cases{
            {},
            {1},
            {1, 2, 3, 4, 5},
            {1, null, 2},
            {1, null, 2, 3},
            {3, 9, 20, null, null, 15, 7},
            {1, 2, 3, null, 4, null, 5, 6},
            {5, 4, 8, 11, null, 13, 4, 7, 2, null, null, null, 1},
        };
        for (const Opt& c : cases) {
            check(BinaryTree<int>::fromLevelOrder(c).toLevelOrder() == c, "level-order round trip");
        }
        check(BinaryTree<int>::fromLevelOrder({1, 2, null, null, null}).toLevelOrder() == Opt{1, 2},
              "trailing nulls are trimmed");
        check(BinaryTree<int>::fromLevelOrder({null, 1}).empty(), "null root gives an empty tree");

        bool threw = false;
        try {
            BinaryTree<int>::fromLevelOrder({1, null, null, 5});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "fromLevelOrder rejects a value with no parent slot (no leak: ASan checks)");
    }

    // ---- Rule of 5
    {
        BinaryTree<int> a = BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, 5});
        BinaryTree<int> b(a);                        // copy constructor
        b.root()->val = 100;
        check(a.root()->val == 1 && b.root()->val == 100, "copy is deep");
        check(b.toLevelOrder() == Opt{100, 2, 3, 4, 5}, "copy has the same shape");

        BinaryTree<int> c = BinaryTree<int>::fromLevelOrder({7});
        c = a;                                       // copy assignment
        check(c.toLevelOrder() == a.toLevelOrder(), "copy assignment");

        BinaryTree<int>& alias = c;
        c = alias;                                   // self-assignment
        check(c.toLevelOrder() == a.toLevelOrder(), "self copy-assignment is a no-op");

        BinaryTree<int> d(std::move(c));             // move constructor
        check(c.empty() && d.toLevelOrder() == a.toLevelOrder(), "move constructor steals the nodes");

        BinaryTree<int> e = BinaryTree<int>::fromLevelOrder({9, 8});
        e = std::move(d);                            // move assignment
        check(d.empty() && e.toLevelOrder() == a.toLevelOrder(), "move assignment");

        BinaryTree<int> empty;
        BinaryTree<int> copyOfEmpty(empty);
        check(copyOfEmpty.empty(), "copy of empty tree");
    }

    // ---- exception safety with a throwing copy constructor
    {
        std::vector<std::optional<Bomb>> vals;
        for (int k = 1; k <= 15; ++k) vals.emplace_back(Bomb(k));
        BinaryTree<Bomb> source = BinaryTree<Bomb>::fromLevelOrder(vals);
        BinaryTree<Bomb> target = BinaryTree<Bomb>::fromLevelOrder({Bomb(70), Bomb(71), Bomb(72)});
        const auto targetBefore = target.toLevelOrder();
        const int liveBefore = Bomb::live;

        Bomb::copiesUntilThrow = 7;   // the 8th node copy throws
        bool threw = false;
        try {
            BinaryTree<Bomb> copy(source);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Bomb::copiesUntilThrow = -1;
        check(threw, "copy constructor propagates the exception");
        check(Bomb::live == liveBefore, "failed copy constructor frees every node it built");

        Bomb::copiesUntilThrow = 7;
        threw = false;
        try {
            target = source;
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Bomb::copiesUntilThrow = -1;
        check(threw, "copy assignment propagates the exception");
        check(Bomb::live == liveBefore, "failed copy assignment leaks nothing");
        check(target.toLevelOrder() == targetBefore, "failed copy assignment leaves the target unchanged");

        Bomb::copiesUntilThrow = 3;
        threw = false;
        try {
            BinaryTree<Bomb>::fromLevelOrder(vals);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Bomb::copiesUntilThrow = -1;
        check(threw && Bomb::live == liveBefore, "failed fromLevelOrder frees the partial tree");
    }

    // ---- deep trees: everything in BinaryTree is iterative, so a million-node chain is fine
    {
        constexpr int kDeep = 1'000'000;
        const Opt right = rightSkewed(kDeep);
        BinaryTree<int> r = BinaryTree<int>::fromLevelOrder(right);
        BinaryTree<int> rCopy(r);
        check(rCopy.toLevelOrder() == right, "1,000,000-node right chain: build, clone, round trip");

        const Opt left = leftSkewed(kDeep);
        BinaryTree<int> l = BinaryTree<int>::fromLevelOrder(left);
        BinaryTree<int> lCopy;
        lCopy = l;
        check(lCopy.toLevelOrder() == left, "1,000,000-node left chain: build, assign, round trip");
        // All four trees are destroyed here by destroyTree. The left chain forces
        // one rotation per node, which is the worst case for the rotation loop.
    }

    // ---- implicit array layout
    {
        static_assert(leftChild(0) == 1 && rightChild(0) == 2, "children of the root");
        static_assert(leftChild(1) == 3 && rightChild(1) == 4, "children of index 1");
        static_assert(parentOf(3) == 1 && parentOf(4) == 1 && parentOf(2) == 0, "parents");
        const std::vector<int> arr{1, 2, 3, 4, 5};   // the sample tree, stored with no pointers
        check(arr[leftChild(1)] == 4 && arr[rightChild(1)] == 5, "array layout of the sample tree");
    }

    // ---- index pool
    {
        IndexTree t;
        const std::int32_t n1 = t.add(1);
        const std::int32_t n2 = t.add(2);
        const std::int32_t n3 = t.add(3);
        const std::int32_t n4 = t.add(4);
        const std::int32_t n5 = t.add(5);
        t.nodes[n1].left = n2;
        t.nodes[n1].right = n3;
        t.nodes[n2].left = n4;
        t.nodes[n2].right = n5;
        std::vector<int> out;
        inorderIndexed(t, 0, out);
        check(out == std::vector<int>{4, 2, 5, 1, 3}, "index-pool inorder matches pointer inorder");
    }

    // ---- general (N-ary) trees
    {
        NaryNode<int>* t = buildNarySample();
        std::vector<int> pre, post;
        naryPreorder(t, pre);
        naryPostorder(t, post);
        check(pre == std::vector<int>{1, 2, 5, 6, 3, 4, 7}, "N-ary preorder");
        check(post == std::vector<int>{5, 6, 2, 3, 7, 4, 1}, "N-ary postorder");
        const std::vector<std::vector<int>> levels{{1}, {2, 3, 4}, {5, 6, 7}};
        check(naryLevelOrder(t) == levels, "N-ary level order");
        check(naryHeight(t) == 2 && naryHeight<int>(nullptr) == -1, "N-ary height in edges");

        TreeNode<int>* b = toLeftChildRightSibling(t);
        std::vector<int> bPre, bIn;
        preorder(b, bPre);
        inorder(b, bIn);
        check(bPre == pre, "LCRS: binary preorder == N-ary preorder");
        check(bIn == post, "LCRS: binary inorder == N-ary postorder");
        check(b->right == nullptr, "LCRS: the root has no right link");
        check(height(b) == 4, "LCRS changes the height: 2 edges become 4");

        NaryNode<int>* back = fromLeftChildRightSibling(b);
        check(sameNary(t, back), "LCRS round trip restores the same N-ary tree");

        TreeNode<int>* forest = new TreeNode<int>(1);
        forest->right = new TreeNode<int>(2);
        bool threw = false;
        try {
            fromLeftChildRightSibling(forest);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "fromLeftChildRightSibling rejects a root with a sibling");

        destroyTree(forest);
        destroyNary(back);
        destroyTree(b);
        destroyNary(t);
        check(toLeftChildRightSibling<int>(nullptr) == nullptr &&
                  fromLeftChildRightSibling<int>(nullptr) == nullptr && naryLevelOrder<int>(nullptr).empty(),
              "N-ary empty-tree cases");
    }

    // ---- N-ary extremes: a 1,000,000-child star and a 1,000,000-deep chain, no recursion anywhere
    {
        constexpr int kWide = 1'000'000;
        auto* star = new NaryNode<int>(0);
        star->children.reserve(kWide);
        for (int k = 1; k <= kWide; ++k) star->children.push_back(new NaryNode<int>(k));
        TreeNode<int>* starBin = toLeftChildRightSibling(star);   // becomes a right chain of 1,000,000
        NaryNode<int>* starBack = fromLeftChildRightSibling(starBin);
        check(starBack->children.size() == static_cast<std::size_t>(kWide) &&
                  starBack->children.back()->val == kWide,
              "1,000,000-child star survives the LCRS round trip");
        check(naryHeight(star) == 1, "a star has height 1");
        destroyNary(starBack);
        destroyTree(starBin);
        destroyNary(star);

        auto* chain = new NaryNode<int>(0);
        NaryNode<int>* last = chain;
        for (int k = 1; k < kWide; ++k) {
            last->children.push_back(new NaryNode<int>(k));
            last = last->children[0];
        }
        TreeNode<int>* chainBin = toLeftChildRightSibling(chain);   // becomes a left chain
        NaryNode<int>* chainBack = fromLeftChildRightSibling(chainBin);
        const NaryNode<int>* walk = chainBack;
        int depth = 0;
        while (!walk->children.empty()) {
            walk = walk->children[0];
            ++depth;
        }
        check(depth == kWide - 1 && walk->val == kWide - 1, "1,000,000-deep N-ary chain survives the round trip");
        destroyNary(chainBack);
        destroyTree(chainBin);
        destroyNary(chain);
    }

    // ---- N-ary exception safety: a copy that throws halfway through a conversion leaks nothing
    {
        auto* t = new NaryNode<Bomb>(Bomb(1));
        for (int v = 2; v <= 6; ++v) t->children.push_back(new NaryNode<Bomb>(Bomb(v)));
        for (int v = 7; v <= 9; ++v) t->children[0]->children.push_back(new NaryNode<Bomb>(Bomb(v)));
        const int liveBefore = Bomb::live;

        Bomb::copiesUntilThrow = 4;
        bool threw = false;
        try {
            toLeftChildRightSibling(t);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Bomb::copiesUntilThrow = -1;
        check(threw && Bomb::live == liveBefore, "failed toLeftChildRightSibling frees the partial copy");

        TreeNode<Bomb>* b = toLeftChildRightSibling(t);
        const int liveWithBinary = Bomb::live;
        Bomb::copiesUntilThrow = 4;
        threw = false;
        try {
            fromLeftChildRightSibling(b);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        Bomb::copiesUntilThrow = -1;
        check(threw && Bomb::live == liveWithBinary, "failed fromLeftChildRightSibling frees the partial copy");
        destroyTree(b);
        destroyNary(t);
        check(Bomb::live == liveBefore - 9, "every Bomb in the N-ary test is destroyed");
    }

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
