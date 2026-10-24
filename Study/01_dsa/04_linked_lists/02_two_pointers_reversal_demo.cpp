// Topic 2 companion code: dummy node, two pointers, Floyd, reversal, recursion.
//
// Build & run (sanitizers catch leaks, double frees, and out-of-bounds/null derefs):
//   g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined 02_two_pointers_reversal_demo.cpp -o demo && ./demo
//
// These are free functions on a bare ListNode (the interview/LeetCode shape).
// Every one of them ports 1:1 into your SinglyLinkedList<T> class as a member function.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct ListNode {
    int val;
    ListNode* next;
    explicit ListNode(int v = 0, ListNode* n = nullptr) : val(v), next(n) {}
};

// ---------------------------------------------------------------------------
// Test helpers (note: build() itself uses the dummy-node technique)
// ---------------------------------------------------------------------------
ListNode* build(const std::vector<int>& v) {
    ListNode dummy;
    ListNode* tail = &dummy;
    for (int x : v) {
        tail->next = new ListNode(x);
        tail = tail->next;
    }
    return dummy.next;
}

std::string str(const ListNode* h) {          // do NOT call on a cyclic list
    if (!h) return "(empty)";
    std::string s;
    for (; h; h = h->next) {
        s += std::to_string(h->val);
        if (h->next) s += " -> ";
    }
    return s;
}

void freeList(ListNode* h) {
    while (h) {
        ListNode* next = h->next;             // save before delete
        delete h;
        h = next;
    }
}

// ---------------------------------------------------------------------------
// 1. Dummy (sentinel) node
// ---------------------------------------------------------------------------
// LC 203. Remove every node whose value == target.
ListNode* removeElements(ListNode* head, int target) {
    ListNode dummy(0, head);                  // lives on the stack; never deleted
    ListNode* prev = &dummy;
    while (prev->next) {
        if (prev->next->val == target) {
            ListNode* doomed = prev->next;
            prev->next = doomed->next;        // unlink; do NOT advance prev (next node may match too)
            delete doomed;
        } else {
            prev = prev->next;
        }
    }
    return dummy.next;
}

// ---------------------------------------------------------------------------
// 2. Two pointers
// ---------------------------------------------------------------------------
// LC 876. Second middle for even length.
ListNode* middleNode(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

// First middle for even length (used to split a list into two halves). Requires head != nullptr.
ListNode* firstMiddle(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head;
    while (fast->next && fast->next->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

// LC 19. Remove the n-th node from the end (1-indexed). Fixed-gap technique.
ListNode* removeNthFromEnd(ListNode* head, int n) {
    if (n < 1) return head;
    ListNode dummy(0, head);
    ListNode* fast = &dummy;
    ListNode* slow = &dummy;
    for (int i = 0; i < n; ++i) {             // open a gap of n nodes
        fast = fast->next;
        if (!fast) return head;               // n > length: nothing to remove
    }
    while (fast->next) {                      // slide the window until fast is on the last node
        fast = fast->next;
        slow = slow->next;
    }
    ListNode* doomed = slow->next;            // slow is the node BEFORE the target
    slow->next = doomed->next;
    delete doomed;
    return dummy.next;
}

// ---------------------------------------------------------------------------
// 3. Floyd's cycle detection
// ---------------------------------------------------------------------------
// LC 141
bool hasCycle(const ListNode* head) {
    const ListNode* slow = head;
    const ListNode* fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return true;        // compare ADDRESSES, never values
    }
    return false;
}

// LC 142. Returns the first node of the cycle, or nullptr if there is none.
ListNode* detectCycle(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {                   // phase 1: they met somewhere inside the cycle
            ListNode* p = head;               // phase 2: head and meeting point walk at the same speed
            while (p != slow) {
                p = p->next;
                slow = slow->next;
            }
            return p;                         // they meet exactly at the cycle's entry
        }
    }
    return nullptr;
}

// Number of nodes in the cycle (0 if acyclic).
int cycleLength(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            int len = 1;
            for (ListNode* p = slow->next; p != slow; p = p->next) ++len;
            return len;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Reversal
// ---------------------------------------------------------------------------
// LC 206, iterative: O(n) time, O(1) space.
ListNode* reverseList(ListNode* head) {
    ListNode* prev = nullptr;
    ListNode* curr = head;
    while (curr) {
        ListNode* next = curr->next;          // 1. save the rest of the chain
        curr->next = prev;                    // 2. flip this link
        prev = curr;                          // 3. advance prev
        curr = next;                          // 4. advance curr
    }
    return prev;                              // prev is the new head
}

// LC 206, recursive: O(n) time, O(n) stack.
ListNode* reverseListRec(ListNode* head) {
    if (!head || !head->next) return head;    // base: empty or single node
    ListNode* newHead = reverseListRec(head->next);
    head->next->next = head;                  // head->next is now the TAIL of the reversed rest; point it back
    head->next = nullptr;                     // head becomes the new tail
    return newHead;
}

// Tail-recursive form. C++ does NOT guarantee tail-call optimisation; GCC/Clang usually
// do it at -O2 but not at -O0, so don't rely on it for long lists.
ListNode* reverseListTail(ListNode* curr, ListNode* prev = nullptr) {
    if (!curr) return prev;
    ListNode* next = curr->next;
    curr->next = prev;
    return reverseListTail(next, curr);
}

// LC 92. Reverse positions [left, right] (1-indexed) in one pass using head insertion.
ListNode* reverseBetween(ListNode* head, int left, int right) {
    if (!head || left >= right) return head;
    ListNode dummy(0, head);
    ListNode* prev = &dummy;
    for (int i = 1; i < left; ++i) prev = prev->next;   // prev = node just before position `left`
    ListNode* curr = prev->next;                         // first node of the segment; becomes its tail
    for (int i = 0; i < right - left; ++i) {
        ListNode* nxt = curr->next;                      // node to move to the front of the segment
        curr->next = nxt->next;
        nxt->next = prev->next;
        prev->next = nxt;
    }
    return dummy.next;
}

// LC 25. Reverse nodes in groups of k; a trailing group shorter than k is left as-is. Requires k >= 1.
ListNode* reverseKGroup(ListNode* head, int k) {
    if (k < 2) return head;
    ListNode dummy(0, head);
    ListNode* groupPrev = &dummy;             // node just before the current group
    while (true) {
        ListNode* kth = groupPrev;
        for (int i = 0; i < k && kth; ++i) kth = kth->next;
        if (!kth) break;                      // fewer than k nodes remain
        ListNode* groupNext = kth->next;      // first node AFTER the group

        ListNode* prev = groupNext;           // seed prev so the group's old head links onward
        ListNode* curr = groupPrev->next;
        while (curr != groupNext) {
            ListNode* nxt = curr->next;
            curr->next = prev;
            prev = curr;
            curr = nxt;
        }
        ListNode* oldGroupHead = groupPrev->next;   // now the group's tail
        groupPrev->next = kth;                      // kth is the group's new head
        groupPrev = oldGroupHead;                   // next group starts after the tail
    }
    return dummy.next;
}

// ---------------------------------------------------------------------------
// 5. Recursion: using the call stack as an implicit stack
// ---------------------------------------------------------------------------
void printReverse(const ListNode* n, std::ostream& os) {
    if (!n) return;
    printReverse(n->next, os);                // go all the way down first...
    os << n->val << ' ';                      // ...then print on the way back up
}

// ---------------------------------------------------------------------------
// 6. Capstone (LC 143): L0 -> L1 -> ... -> Ln  becomes  L0 -> Ln -> L1 -> Ln-1 -> ...
//    middle + reverse + interleave
// ---------------------------------------------------------------------------
void reorderList(ListNode* head) {
    if (!head || !head->next) return;
    ListNode* mid = firstMiddle(head);        // 1. find the first middle
    ListNode* second = reverseList(mid->next);// 2. reverse the second half
    mid->next = nullptr;                      //    and cut the list in two
    ListNode* first = head;                   // 3. interleave
    while (second) {
        ListNode* n1 = first->next;
        ListNode* n2 = second->next;
        first->next = second;
        second->next = n1;
        first = n1;
        second = n2;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;

void check(bool ok, const std::string& name) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

void expectList(ListNode* got, const std::string& want, const std::string& name) {
    std::string have = str(got);
    check(have == want, name + "  (got: " + have + ", want: " + want + ")");
    freeList(got);
}

int main() {
    // --- dummy node: removeElements
    expectList(removeElements(build({1, 2, 6, 3, 4, 5, 6}), 6), "1 -> 2 -> 3 -> 4 -> 5", "removeElements basic");
    expectList(removeElements(build({7, 7, 7}), 7), "(empty)", "removeElements all match");
    expectList(removeElements(build({6, 6, 1}), 6), "1", "removeElements leading run");
    expectList(removeElements(nullptr, 1), "(empty)", "removeElements empty");

    // --- two pointers: middle
    {
        ListNode* a = build({1, 2, 3, 4, 5});
        check(middleNode(a)->val == 3, "middle odd");
        check(firstMiddle(a)->val == 3, "firstMiddle odd");
        freeList(a);
        ListNode* b = build({1, 2, 3, 4, 5, 6});
        check(middleNode(b)->val == 4, "middle even (second)");
        check(firstMiddle(b)->val == 3, "firstMiddle even (first)");
        freeList(b);
        ListNode* c = build({1});
        check(middleNode(c)->val == 1 && firstMiddle(c)->val == 1, "middle single");
        freeList(c);
        check(middleNode(nullptr) == nullptr, "middle empty");
    }

    // --- two pointers: nth from end
    expectList(removeNthFromEnd(build({1, 2, 3, 4, 5}), 2), "1 -> 2 -> 3 -> 5", "nthFromEnd middle");
    expectList(removeNthFromEnd(build({1, 2, 3, 4, 5}), 5), "2 -> 3 -> 4 -> 5", "nthFromEnd removes head");
    expectList(removeNthFromEnd(build({1, 2, 3, 4, 5}), 1), "1 -> 2 -> 3 -> 4", "nthFromEnd removes tail");
    expectList(removeNthFromEnd(build({1}), 1), "(empty)", "nthFromEnd single");
    expectList(removeNthFromEnd(build({1, 2}), 3), "1 -> 2", "nthFromEnd n > length");
    expectList(removeNthFromEnd(build({1, 2}), 0), "1 -> 2", "nthFromEnd n == 0");

    // --- Floyd
    {
        ListNode* a = build({1, 2, 3, 4, 5});
        check(!hasCycle(a) && detectCycle(a) == nullptr && cycleLength(a) == 0, "acyclic");
        ListNode* tail = a;
        while (tail->next) tail = tail->next;
        ListNode* entry = a->next->next;               // node 3
        tail->next = entry;                            // 1 -> 2 -> 3 -> 4 -> 5 -> (back to 3)
        check(hasCycle(a), "cycle detected");
        check(detectCycle(a) == entry, "cycle entry found");
        check(cycleLength(a) == 3, "cycle length 3");
        tail->next = nullptr;                          // break the cycle BEFORE freeing
        freeList(a);

        ListNode* b = build({1, 2, 3});
        ListNode* btail = b->next->next;
        btail->next = b;                               // whole list is the cycle (entry == head)
        check(hasCycle(b) && detectCycle(b) == b && cycleLength(b) == 3, "cycle entry == head");
        btail->next = nullptr;
        freeList(b);

        ListNode* c = build({1});
        c->next = c;                                   // self loop
        check(hasCycle(c) && detectCycle(c) == c && cycleLength(c) == 1, "self loop");
        c->next = nullptr;
        freeList(c);

        check(!hasCycle(nullptr) && detectCycle(nullptr) == nullptr, "cycle on empty");

        // equal values must not fool the detector (it compares addresses)
        ListNode* d = build({7, 7, 7, 7});
        check(!hasCycle(d), "duplicate values, no cycle");
        freeList(d);
    }

    // --- reversal
    expectList(reverseList(build({1, 2, 3, 4, 5})), "5 -> 4 -> 3 -> 2 -> 1", "reverse iterative");
    expectList(reverseList(build({1})), "1", "reverse iterative single");
    expectList(reverseList(nullptr), "(empty)", "reverse iterative empty");
    expectList(reverseListRec(build({1, 2, 3, 4, 5})), "5 -> 4 -> 3 -> 2 -> 1", "reverse recursive");
    expectList(reverseListRec(build({1, 2})), "2 -> 1", "reverse recursive two");
    expectList(reverseListTail(build({1, 2, 3, 4, 5})), "5 -> 4 -> 3 -> 2 -> 1", "reverse tail-recursive");

    expectList(reverseBetween(build({1, 2, 3, 4, 5}), 2, 4), "1 -> 4 -> 3 -> 2 -> 5", "reverseBetween middle");
    expectList(reverseBetween(build({1, 2, 3, 4, 5}), 1, 5), "5 -> 4 -> 3 -> 2 -> 1", "reverseBetween whole");
    expectList(reverseBetween(build({1, 2, 3, 4, 5}), 1, 2), "2 -> 1 -> 3 -> 4 -> 5", "reverseBetween from head");
    expectList(reverseBetween(build({1, 2, 3, 4, 5}), 4, 5), "1 -> 2 -> 3 -> 5 -> 4", "reverseBetween at tail");
    expectList(reverseBetween(build({1, 2, 3}), 2, 2), "1 -> 2 -> 3", "reverseBetween left == right");
    expectList(reverseBetween(build({5}), 1, 1), "5", "reverseBetween single");

    expectList(reverseKGroup(build({1, 2, 3, 4, 5}), 2), "2 -> 1 -> 4 -> 3 -> 5", "kGroup k=2");
    expectList(reverseKGroup(build({1, 2, 3, 4, 5}), 3), "3 -> 2 -> 1 -> 4 -> 5", "kGroup k=3");
    expectList(reverseKGroup(build({1, 2, 3, 4, 5}), 5), "5 -> 4 -> 3 -> 2 -> 1", "kGroup k=n");
    expectList(reverseKGroup(build({1, 2, 3, 4, 5}), 6), "1 -> 2 -> 3 -> 4 -> 5", "kGroup k>n");
    expectList(reverseKGroup(build({1, 2, 3, 4, 5}), 1), "1 -> 2 -> 3 -> 4 -> 5", "kGroup k=1");
    expectList(reverseKGroup(build({1, 2, 3, 4, 5, 6}), 3), "3 -> 2 -> 1 -> 6 -> 5 -> 4", "kGroup exact multiple");
    expectList(reverseKGroup(nullptr, 3), "(empty)", "kGroup empty");

    // --- recursion: print in reverse without modifying the list
    {
        ListNode* a = build({1, 2, 3, 4});
        std::ostringstream os;
        printReverse(a, os);
        check(os.str() == "4 3 2 1 ", "printReverse");
        check(str(a) == "1 -> 2 -> 3 -> 4", "printReverse leaves list intact");
        freeList(a);
    }

    // --- capstone
    {
        ListNode* a = build({1, 2, 3, 4});
        reorderList(a);
        expectList(a, "1 -> 4 -> 2 -> 3", "reorder even");
        ListNode* b = build({1, 2, 3, 4, 5});
        reorderList(b);
        expectList(b, "1 -> 5 -> 2 -> 4 -> 3", "reorder odd");
        ListNode* c = build({1, 2});
        reorderList(c);
        expectList(c, "1 -> 2", "reorder two");
        ListNode* d = build({1});
        reorderList(d);
        expectList(d, "1", "reorder single");
    }

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
