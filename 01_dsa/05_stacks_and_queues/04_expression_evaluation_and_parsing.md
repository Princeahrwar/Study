# DSA 05.04: Expression Evaluation and Parsing

*Part of the [Stacks and queues plan](00_plan.md) · Time: about 7 hours (3 reading, 4 problems) · You need: [Stack and queue fundamentals](01_stack_and_queue_fundamentals.md) (the stack contract) and [The monotonic stack](02_monotonic_stack.md) (one pass, every element pushed and popped once) · Related: [Strings](../01_arrays_and_strings/07_string_basics.md) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- say why nested structure is a stack problem, and name what the stack holds in each of the classic problems;
- solve balanced brackets, the minimum removal, the longest valid run and the removal of invalid parentheses;
- evaluate reverse Polish notation and write the two calculators of LeetCode 227 and 224;
- convert infix to postfix with the shunting-yard method, handling precedence, right associativity (`^`) and unary minus, and evaluate it;
- write the same calculator as a recursive-descent parser, say how the two relate, and say where each one breaks;
- solve decode string, simplify path, asteroid collision, validate stack sequences, backspace compare and adjacent duplicates with a stack.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested against independent references: 8,527,227 checks passed under AddressSanitizer with UBSan (GCC 13.3), and the same program was also built at `-O2` with GCC and with clang 18. The references are brute forces (reduce `()` pairs until nothing is left, try every subset of characters, simulate collisions one at a time, list every legal pop order) and a generator of random expression trees that prints each tree with and without redundant brackets and spaces and knows its value. The three evaluators of sections 5 and 6 were also run on about 700,000 random and exhaustive strings, including malformed ones, and had to agree on whether the string is legal and on its value. The only timing-like number is a stack depth, measured once on one machine: an x86-64 VM, Ubuntu 24.04, GCC 13.3 with `-O2`, an 8 MiB stack (`ulimit -s` printed 8192). Problem numbers and titles were checked against pages that list them (see Sources). Difficulty labels are left out because I could not verify them: the only page that gave them contradicted itself on several problems.

All listings need these headers: `<algorithm>`, `<climits>`, `<stdexcept>`, `<string>`, `<utility>` and `<vector>`.

---

## 1. The idea

### 1.1 Nesting is last in, first out

In `((a)(b))` the first bracket opened is the last one closed. In `2 * (3 + 4)` the `3 + 4` must be finished before the `2 *` can be used. In `3[a2[c]]` the inner `2[c]` is expanded before the outer `3[...]`. In each case the thing opened most recently is the thing that must be finished first, which is the stack contract of 05.01. The stack is the memory of "what is still open".

| Problem family | What the stack holds | An item leaves when |
|---|---|---|
| Brackets | the closers still owed | the matching closer arrives |
| Postfix (RPN) | numbers waiting for their operator | an operator arrives |
| Calculators | finished terms, or the sign of each open group | the next `+`, `-` or `)` ends them |
| Shunting-yard | operators waiting for their right operand | an operator that binds no tighter arrives |
| Decode, path | text before a bracket, directory names | `]`, `..` |
| Simulations | survivors | a collision, a cancel |

### 1.2 Three habits

1. One pass from left to right. Each character does at most one push and one pop of its own, so the time is O(n) and the stack is O(n).
2. The bugs sit at the **end of the input** (the last number is never flushed) and at **ties** (which operator goes first when precedences are equal).
3. Decide what happens on bad input before you write the loop: an empty stack on a pop, a stack that is not empty at the end.

---

## 2. Brackets

### 2.1 Valid parentheses (LeetCode 20) and the minimum removal (1249)

Push the **closer you need**, not the opener: then the test on a closing character is "equal to the top", with no lookup table. The string is balanced when no closer fails the test and the stack is empty at the end.

```cpp
// 20: three kinds of brackets; other characters are ignored.
bool isBalanced(const std::string& s) {
    std::string need;                       // closers still owed, innermost last
    for (char c : s) {
        if (c == '(') need.push_back(')');
        else if (c == '[') need.push_back(']');
        else if (c == '{') need.push_back('}');
        else if (c == ')' || c == ']' || c == '}') {
            if (need.empty() || need.back() != c) return false;
            need.pop_back();
        }
    }
    return need.empty();
}

// 1249: keep everything except the fewest parentheses that have to go.
std::string minRemoveToValid(const std::string& s) {
    std::vector<bool> drop(s.size(), false);
    std::vector<int> open;                  // positions of '(' still waiting
    for (int i = 0; i < (int)s.size(); ++i) {
        if (s[i] == '(') open.push_back(i);
        else if (s[i] == ')') {
            if (open.empty()) drop[i] = true;        // nothing to close
            else open.pop_back();
        }
    }
    for (int i : open) drop[i] = true;               // never closed
    std::string out;
    for (int i = 0; i < (int)s.size(); ++i)
        if (!drop[i]) out.push_back(s[i]);
    return out;
}
```

| Char | Action | Stack of needed closers |
|---|---|---|
| `(` | push ) | `)` |
| `[` | push ] | `)]` |
| `)` | top is ], need ): not balanced | `)]` |

There are three ways to fail: the wrong closer (above), a closer with an empty stack, and a non-empty stack at the end. With a single kind of bracket the stack would hold identical symbols, so only its height matters and a counter is enough (section 2.3 uses this).

`minRemoveToValid` (LeetCode 1249) keeps the **positions** of the open brackets instead. A `)` with no `(` waiting can never be matched, so it is marked at once; every `(` left on the stack at the end can never be matched either. Each marked bracket is forced, so the result is as long as possible. Several outputs can be correct for the same input, so the tests check the properties (valid, a subsequence of the input, longest possible), not one particular string.

### 2.2 Longest valid parentheses (LeetCode 32)

Keep a stack of positions, but start it with a **base**: `-1`, the position just before the current run. On `(` push its position. On `)` pop; if the stack is now empty this `)` is a wall (nothing can match across it), so push its position as the new base; otherwise the run ends at `i` and starts just after the new top, so its length is `i - top`.

```cpp
// 32: the stack holds the position just before the current valid run.
int longestValid(const std::string& s) {
    std::vector<int> st{-1};
    int best = 0;
    for (int i = 0; i < (int)s.size(); ++i) {
        if (s[i] == '(') { st.push_back(i); continue; }
        st.pop_back();
        if (st.empty()) st.push_back(i);             // this ')' is a wall
        else best = std::max(best, i - st.back());
    }
    return best;
}

// Same answer with two counters and no stack: scan left to right, then right to left.
int longestValidTwoPass(const std::string& s) {
    int best = 0, n = (int)s.size();
    for (int pass = 0; pass < 2; ++pass) {
        char opener = pass == 0 ? '(' : ')';
        int a = 0, b = 0;                            // openers and closers seen
        for (int j = 0; j < n; ++j) {
            char c = pass == 0 ? s[j] : s[n - 1 - j];
            if (c == opener) ++a; else ++b;
            if (a == b) best = std::max(best, 2 * b);
            else if (b > a) a = b = 0;               // a wall: start again
        }
    }
    return best;
}
```

| i | Char | Action | Stack | Best |
|---|---|---|---|---|
| 0 | `)` | pop, empty, so wall: push 0 | `[0]` | 0 |
| 1 | `(` | push | `[0, 1]` | 0 |
| 2 | `)` | pop, run of length 2 - 0 = 2 | `[0]` | 2 |
| 3 | `(` | push | `[0, 3]` | 2 |
| 4 | `)` | pop, run of length 4 - 0 = 4 | `[0]` | 4 |
| 5 | `)` | pop, empty, so wall: push 5 | `[5]` | 4 |

### 2.3 The same with two counters

`longestValidTwoPass` needs no stack. Scanning left to right, count openers `a` and closers `b`: when they are equal the run so far is valid (length `2b`); when closers outnumber openers a wall has been hit, so both counters restart. That pass misses runs such as `(()` where the openers are in surplus at the end; the right-to-left pass, with the roles of the two characters swapped, finds them. Both versions were tested against an O(n^2) brute force on every string over `()` up to length 14 and on 3,000 random strings up to length 150.

### 2.4 Remove invalid parentheses (LeetCode 301)

Here a stack is not enough, because many different strings are correct and all must be listed. First one pass counts the misplaced brackets: `rmL` unmatched `(` and `rmR` unmatched `)` (the same counters as above). Then a depth-first search decides, for each run of equal brackets, how many to drop, never dropping more than the counts allow and abandoning a branch the moment the balance would go negative. Treating a **run** as one decision is what makes every answer appear once: dropping the first or the second of `((` gives the same string, and the search tries it once.

```cpp
static void dfsRemove(const std::string& s, int i, int rmL, int rmR, int bal,
                      std::string& cur, std::vector<std::string>& out) {
    int n = (int)s.size();
    if (i == n) {
        if (rmL == 0 && rmR == 0 && bal == 0) out.push_back(cur);
        return;
    }
    char c = s[i];
    if (c != '(' && c != ')') {
        cur.push_back(c);
        dfsRemove(s, i + 1, rmL, rmR, bal, cur, out);
        cur.pop_back();
        return;
    }
    int j = i;
    while (j < n && s[j] == c) ++j;                  // a run of equal brackets
    int run = j - i, len0 = (int)cur.size();
    for (int drop = 0; drop <= std::min(run, c == '(' ? rmL : rmR); ++drop) {
        int keep = run - drop;
        int nb = bal + (c == '(' ? keep : -keep);
        if (nb < 0) continue;                        // a ')' with nothing to close
        cur.append(keep, c);
        dfsRemove(s, j, c == '(' ? rmL - drop : rmL, c == ')' ? rmR - drop : rmR, nb, cur, out);
        cur.resize(len0);
    }
}

// 301: every valid string reachable with the fewest removals, each listed once.
std::vector<std::string> removeInvalid(const std::string& s) {
    int rmL = 0, rmR = 0;                            // misplaced '(' and ')'
    for (char c : s) {
        if (c == '(') ++rmL;
        else if (c == ')') { if (rmL > 0) --rmL; else ++rmR; }
    }
    std::vector<std::string> out;
    std::string cur;
    dfsRemove(s, 0, rmL, rmR, 0, cur, out);
    return out;
}
```

For `()())()` the search returns `(())()` and `()()()` and for `(a)())()` it returns `(a())()` and `(a)()()` (the order of the list is not fixed; here it is sorted). It was compared with a 2^n subset brute force on every string over `()a` up to length 9 and on 400 random strings of length 10 to 13: same set of answers, and no string listed twice. The worst case is exponential, which is fine because the answer itself can be.

---

## 3. Reverse Polish notation (LeetCode 150)

### 3.1 What postfix is

In postfix the operator comes after its operands: `3 4 +` is `3 + 4`. The order of the tokens is the order of the work, so there are no brackets and no precedence rules: `(1 + 2) * 3` is `1 2 + 3 *` and `1 + 2 * 3` is `1 2 3 * +`. Evaluation is a stack of numbers: a number is pushed; an operator pops its operands, computes, and pushes the result. At the end one number is left.

```cpp
// 150: tokens such as {"2", "1", "+", "3", "*"}; "/" truncates toward zero.
long long evalRPN(const std::vector<std::string>& tokens) {
    std::vector<long long> st;
    for (const std::string& t : tokens) {
        if (t == "+" || t == "-" || t == "*" || t == "/") {
            long long b = st.back(); st.pop_back();  // the right operand comes off first
            long long a = st.back(); st.pop_back();
            if (t == "+") a += b;
            else if (t == "-") a -= b;
            else if (t == "*") a *= b;
            else a /= b;
            st.push_back(a);
        } else {
            st.push_back(std::stoll(t));
        }
    }
    return st.back();
}
```

| Token | Action | Stack |
|---|---|---|
| `3` | push | `[3]` |
| `4` | push | `[3, 4]` |
| `2` | push | `[3, 4, 2]` |
| `*` | pop 2 and 4, push 4 * 2 = 8 | `[3, 8]` |
| `+` | pop 8 and 3, push 3 + 8 = 11 | `[11]` |
| `1` | push | `[11, 1]` |
| `-` | pop 1 and 11, push 11 - 1 = 10 | `[10]` |

### 3.2 Details that matter

- **The right operand comes off first.** In `3 4 -` the top of the stack is 4, so `b = 4` and `a = 3`, and the answer is `a - b = -1`. Swapping the two gives `+1`, and gives `0` instead of `4` for `8 2 /`.
- **Division.** C++ integer division truncates toward zero: `-7 / 2` is `-3`. Python's `//` floors, so `-7 // 2` is `-4`, and a prototype in Python can disagree with the C++ on negative operands. LeetCode 150 asks for truncation toward zero.
- **A minus sign is not always an operator.** `-11` is a number token, so the test must compare the whole token with `"-"`, not look at its first character.
- Integer literals go through `std::stoll`; the code assumes the input is valid postfix. Section 5 shows the checked version.

---

## 4. The calculators

### 4.1 No brackets (LeetCode 227)

`+` and `-` just add terms; `*` and `/` bind tighter and must be applied to the term before them. Keep a stack of **finished terms**. When a number ends, look at the operator that came **before** it: for `+` push the number, for `-` push its negation, for `*` or `/` combine it into the top. The answer is the sum of the stack.

Two details. A fake `+` at the end of the string flushes the last number (forget it and the last number is silently lost). And `3 - 6 / 4` works because `-6 / 4` and `-(6 / 4)` are both `-1` when division truncates; with floor division they would differ.

```cpp
// 227 (digits, + - * /, spaces): a stack of signed terms; the answer is their sum.
long long calc2(const std::string& s) {
    std::vector<long long> terms;
    long long num = 0;
    char op = '+';                                   // the operator before num
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : '+';          // a fake '+' flushes the last number
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); continue; }
        if (c == ' ') continue;
        if (op == '+') terms.push_back(num);
        else if (op == '-') terms.push_back(-num);
        else if (op == '*') terms.back() *= num;
        else terms.back() /= num;
        op = c;
        num = 0;
    }
    long long sum = 0;
    for (long long t : terms) sum += t;
    return sum;
}

// The same with one variable instead of the stack: only the newest term can still change.
long long calc2OnePass(const std::string& s) {
    long long total = 0, last = 0, num = 0;
    char op = '+';
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : '+';
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); continue; }
        if (c == ' ') continue;
        if (op == '+') { total += last; last = num; }
        else if (op == '-') { total += last; last = -num; }
        else if (op == '*') last *= num;
        else last /= num;
        op = c;
        num = 0;
    }
    return total + last;
}

// 224 (digits, + - ( ), spaces; '-' may start a bracket group): a stack of group signs.
long long calc1(const std::string& s) {
    std::vector<int> scale{1};                       // the sign carried by the current group
    long long res = 0, num = 0;
    int sign = 1;                                    // the sign in front of num
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : '+';
        if (c >= '0' && c <= '9') num = num * 10 + (c - '0');
        else if (c == ' ') continue;
        else if (c == '(') { scale.push_back(scale.back() * sign); sign = 1; }
        else {                                       // '+', '-' or ')' ends a number
            res += sign * scale.back() * num;
            num = 0;
            if (c == ')') scale.pop_back();
            else sign = c == '+' ? 1 : -1;
        }
    }
    return res;
}
```

| Operator before the number | Number | Action | Terms |
|---|---|---|---|
| `+` | 2 | push 2 | `[2]` |
| `*` | 3 | multiply the top by 3 | `[6]` |
| `-` | 4 | push -4 | `[6, -4]` |
| `*` | 5 | multiply the top by 5 | `[6, -20]` |

Only the **newest** term can still change, so the stack collapses into one variable: `calc2OnePass` keeps `total` (everything finished) and `last` (the term still open). Both versions agree on 150,000 random expressions.

### 4.2 Brackets, but only `+` and `-` (LeetCode 224)

A group preceded by `-` flips every sign inside it, groups inside groups multiply. So keep a stack of **group signs**; the sign of a number is its own sign times the top of that stack. On `(` push `top * sign` and reset the sign to `+`; on `)` pop. A `-` at the very start, as in `-(2+3)`, works because the first `-` flushes a zero.

| Char | Action | Result | Sign | Group signs |
|---|---|---|---|---|
| `-` | add 1 to the result | 1 | -1 | `[1]` |
| `(` | push group sign -1 | 1 | 1 | `[1, -1]` |
| `+` | add -2 to the result | -1 | 1 | `[1, -1]` |
| `)` | add -3 to the result, pop the group | -4 | 1 | `[1]` |
| end | add 0 to the result | -4 | 1 | `[1]` |

### 4.3 Where the tricks stop

With `*`, `/` and brackets together (LeetCode 772, "Basic Calculator III", premium) neither trick composes: a bracket can be a factor of a term. At that point use the general methods of the next two sections.

---

## 5. The shunting-yard method

### 5.1 The idea

Dijkstra's method turns infix into postfix in one scan with a stack of **operators**. Numbers go straight to the output. An operator cannot run yet, because its right operand has not arrived, so it waits on the stack. It is released when an operator arrives that proves it must run first:

> Before pushing operator `o`, pop every operator `p` on the stack with `prec(p) > prec(o)`, or with equal precedence when `o` is left-associative.

A `(` is a barrier that nothing pops past; a `)` pops down to its `(` and discards both. At the end everything left is popped.

| Operator | Precedence | Associativity |
|---|---|---|
| `+` `-` | 1 | left |
| `*` `/` | 2 | left |
| unary `-` | 3 | prefix |
| `^` | 4 | right |

Associativity is the tie rule. `8 - 3 - 2` means `(8 - 3) - 2`, so the first `-` must go before the second arrives. `2 ^ 3 ^ 2` means `2 ^ (3 ^ 2)`, so the first `^` must **wait**: that is why the rule pops on equal precedence only for left-associative operators. Unary minus sits below `^` because `-2 ^ 2` is `-4` in ordinary maths.

### 5.2 Unary minus

The same character `-` is two operators. It is unary when an operand is due: at the start, after `(`, or after another operator. Keep a flag `want` ("is an operand due?"): it is true at the start, after an operator and after `(`, and false after a number or `)`. A `-` seen while `want` is true is pushed as `u` and pops nothing, because a prefix operator has nothing finished on its left. The same flag detects most syntax errors: a number while `want` is false (`2 3`), a binary operator while `want` is true (`2 * * 3`, `+ 1`), an empty `()`, an expression that ends while `want` is true (`1 +`).

### 5.3 The code

Arithmetic goes through one checked function, so overflow, division by zero and a negative exponent are exceptions instead of silent wrong answers (`LLONG_MIN / -1` is undefined behaviour in C++, and signed overflow too).

```cpp
// Checked arithmetic, shared by everything below. Errors are exceptions.
static long long mulChecked(long long a, long long b) {
    long long r;
    if (__builtin_mul_overflow(a, b, &r)) throw std::overflow_error("overflow");
    return r;
}

long long apply(char op, long long a, long long b) {
    long long r;
    switch (op) {
    case '+': if (__builtin_add_overflow(a, b, &r)) throw std::overflow_error("overflow"); return r;
    case '-': if (__builtin_sub_overflow(a, b, &r)) throw std::overflow_error("overflow"); return r;
    case '*': return mulChecked(a, b);
    case '/':
        if (b == 0) throw std::domain_error("divide by zero");
        if (a == LLONG_MIN && b == -1) throw std::overflow_error("overflow");
        return a / b;                                // truncates toward zero
    case '^': {                                      // square and multiply
        if (b < 0) throw std::domain_error("negative exponent");
        r = 1;
        while (b > 0) {
            if (b & 1) r = mulChecked(r, a);
            b >>= 1;
            if (b) a = mulChecked(a, a);
        }
        return r;
    }
    }
    throw std::logic_error("unknown operator");
}
```

```cpp
struct Tok {
    char kind;          // 'n' number, 'u' unary minus, or the operator or bracket itself
    long long val;
};

std::vector<Tok> tokenize(const std::string& s) {
    std::vector<Tok> out;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == ' ') {
            ++i;
        } else if (c >= '0' && c <= '9') {
            long long v = 0;
            for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
                if (__builtin_mul_overflow(v, 10, &v) || __builtin_add_overflow(v, s[i] - '0', &v))
                    throw std::overflow_error("number too large");
            out.push_back({'n', v});
        } else if (std::string("+-*/^()").find(c) != std::string::npos) {
            out.push_back({c, 0});
            ++i;
        } else {
            throw std::invalid_argument("bad character");
        }
    }
    return out;
}
```

```cpp
int prec(char kind) {
    switch (kind) {
    case '+': case '-': return 1;
    case '*': case '/': return 2;
    case 'u': return 3;                              // unary minus binds below '^'
    case '^': return 4;
    }
    return 0;
}

// Calls emit(token) for the tokens in postfix order.
template <class Emit>
void shunt(const std::vector<Tok>& in, Emit emit) {
    std::vector<Tok> ops;
    bool want = true;                                // is an operand due next?
    for (Tok t : in) {
        if (t.kind == 'n') {
            if (!want) throw std::invalid_argument("syntax");
            emit(t);
            want = false;
        } else if (t.kind == '(') {
            if (!want) throw std::invalid_argument("syntax");
            ops.push_back(t);
        } else if (t.kind == ')') {
            if (want) throw std::invalid_argument("syntax");
            while (!ops.empty() && ops.back().kind != '(') { emit(ops.back()); ops.pop_back(); }
            if (ops.empty()) throw std::invalid_argument("unmatched )");
            ops.pop_back();                          // discard the '('
        } else if (want) {                           // an operator where an operand is due
            if (t.kind != '-') throw std::invalid_argument("syntax");
            ops.push_back({'u', 0});                 // a prefix operator waits and pops nothing
        } else {                                     // a binary operator
            while (!ops.empty() && ops.back().kind != '(' &&
                   (prec(ops.back().kind) > prec(t.kind) ||
                    (prec(ops.back().kind) == prec(t.kind) && t.kind != '^')))
                { emit(ops.back()); ops.pop_back(); }
            ops.push_back(t);
            want = true;
        }
    }
    if (want) throw std::invalid_argument("syntax");
    while (!ops.empty()) {
        if (ops.back().kind == '(') throw std::invalid_argument("unmatched (");
        emit(ops.back());
        ops.pop_back();
    }
}

// One step of a postfix evaluator (the stack of section 3, with unary minus added).
void step(std::vector<long long>& vals, const Tok& t) {
    if (t.kind == 'n') { vals.push_back(t.val); return; }
    if (vals.empty()) throw std::invalid_argument("syntax");
    long long b = vals.back(); vals.pop_back();
    if (t.kind == 'u') { vals.push_back(apply('-', 0, b)); return; }
    if (vals.empty()) throw std::invalid_argument("syntax");
    long long a = vals.back(); vals.pop_back();
    vals.push_back(apply(t.kind, a, b));
}

std::vector<Tok> toPostfix(const std::string& s) {
    std::vector<Tok> out;
    shunt(tokenize(s), [&](Tok t) { out.push_back(t); });
    return out;
}

long long evalPostfix(const std::vector<Tok>& post) {
    std::vector<long long> vals;
    for (const Tok& t : post) step(vals, t);
    if (vals.size() != 1) throw std::invalid_argument("syntax");
    return vals[0];
}

// Infix straight to a value: the same emit, but each token is executed at once.
long long evalInfix(const std::string& s) {
    std::vector<long long> vals;
    shunt(tokenize(s), [&](Tok t) { step(vals, t); });
    return vals.back();
}
```

`shunt` does not decide what to do with the output: it calls `emit` for each token in postfix order. `toPostfix` collects the tokens; `evalInfix` passes `emit` a function that executes each token at once on a stack of values, which is Dijkstra's two-stack evaluator and needs no postfix array.

| Token | Action | Output | Operator stack |
|---|---|---|---|
| `3` | number to the output | `3` | empty |
| `+` | nothing to pop, push | `3` | `+` |
| `4` | number to the output | `3 4` | `+` |
| `*` | nothing to pop, push | `3 4` | `+ *` |
| `2` | number to the output | `3 4 2` | `+ *` |
| `-` | pop `*` `+`, then push | `3 4 2 * +` | `-` |
| `1` | number to the output | `3 4 2 * + 1` | `-` |
| end | pop everything | `3 4 2 * + 1 -` | empty |

Postfix forms of a few more (unary minus is written `u`):

| Expression | Postfix | Value |
|---|---|---|
| `3 + 4 * 2 - 1` | `3 4 2 * + 1 -` | 10 |
| `2 ^ 3 ^ 2` | `2 3 2 ^ ^` | 512 |
| `-2 ^ 2` | `2 2 ^ u` | -4 |
| `(1 + 2) * 3` | `1 2 + 3 *` | 9 |
| `8 - 3 - 2` | `8 3 - 2 -` | 3 |
| `-(2 + 3) * 4` | `2 3 + u 4 *` | -20 |

Each value was also checked against Python's own evaluation of the same expression (with `^` written `**`), and the recursive-descent parser of the next section gives the same values.

### 5.4 Complexity

Each token is pushed and popped at most once, so O(n) time and O(n) space. The stack of operators grows with the nesting depth of brackets (and with a chain of `^` or of prefix minus signs), not with the length of the numbers.

---

## 6. Recursive descent

### 6.1 One function per precedence level

Write the grammar with the tightest-binding level last:

```
expr  := term  (('+' | '-') term)*
term  := unary (('*' | '/') unary)*
unary := '-' unary | power
power := atom ('^' unary)?
atom  := number | '(' expr ')'
```

Each rule becomes a function that calls the rule below it. A loop gives left associativity (`8 - 3 - 2` is a loop in `expr`). Recursion gives right associativity (`power` calls `unary` on its right side, so `2 ^ 3 ^ 2` nests to the right). Unary minus is a rule of its own, placed above `power` so that `-2 ^ 2` is `-(2 ^ 2)`, matching the shunting-yard table.

```cpp
class Parser {
    const std::vector<Tok>& t_;
    size_t p_ = 0;
    bool at(char kind) const { return p_ < t_.size() && t_[p_].kind == kind; }

    long long expr() {                               // expr := term (('+' | '-') term)*
        long long v = term();
        while (at('+') || at('-')) { char op = t_[p_++].kind; v = apply(op, v, term()); }
        return v;
    }
    long long term() {                               // term := unary (('*' | '/') unary)*
        long long v = unary();
        while (at('*') || at('/')) { char op = t_[p_++].kind; v = apply(op, v, unary()); }
        return v;
    }
    long long unary() {                              // unary := '-' unary | power
        if (at('-')) { ++p_; return apply('-', 0, unary()); }
        return power();
    }
    long long power() {                              // power := atom ('^' unary)?
        long long b = atom();
        if (at('^')) { ++p_; return apply('^', b, unary()); }
        return b;
    }
    long long atom() {                               // atom := number | '(' expr ')'
        if (at('n')) return t_[p_++].val;
        if (at('(')) {
            ++p_;
            long long v = expr();
            if (!at(')')) throw std::invalid_argument("expected )");
            ++p_;
            return v;
        }
        throw std::invalid_argument("expected a number or (");
    }

public:
    explicit Parser(const std::vector<Tok>& t) : t_(t) {}
    long long parse() {
        long long v = expr();
        if (p_ != t_.size()) throw std::invalid_argument("extra input");
        return v;
    }
};

long long evalDescent(const std::string& s) {
    std::vector<Tok> toks = tokenize(s);
    return Parser(toks).parse();
}
```

### 6.2 The same stack in another place

In shunting-yard the operators that are waiting live in a vector. Here they live in the **call stack**: while `expr` waits for the right operand of a `+`, its frame is the pending `+`. The two methods do the same work in the same order, which is why they agree on every input in the tests. A bracket costs one recursive round trip, `expr` to `atom` and back to `expr`.

### 6.3 Where recursive descent breaks

Nesting depth costs call-stack space. On the machine described above, `evalDescent` on `((( ... 1 ... )))` with 50,000 pairs of brackets worked and with 70,000 pairs the program was killed by a segmentation fault; `evalInfix` on one million pairs worked, because its stack is a vector on the heap. A parser that reads input you do not control, such as a request body, should either limit the depth or use the explicit stack.

### 6.4 Choosing

| Situation | Choose |
|---|---|
| A flat table of operators with precedences, and you may add more | Shunting-yard: a new operator is one line in `prec` |
| You also want postfix, or a tree, as output | Shunting-yard: change `emit` |
| A real grammar: statements, calls, optional parts, good error messages | Recursive descent |
| Untrusted deeply nested input | Explicit stack, or a depth limit |
| An interview with a 20-minute slot | Whichever you can write without bugs; recursive descent is shorter to explain |

---

## 7. Stacks of strings and frames

### 7.1 Decode string (LeetCode 394)

`3[a2[c]]` means `a` followed by two `c` repeated three times. When a `[` opens a group, save what you have built so far and the repeat count in a **frame**, and start a fresh current text. When `]` closes it, repeat the current text, glue it to the saved text, and make that the current text.

| Char | Action | Current text | Stack |
|---|---|---|---|
| `2` | count so far 2 | empty | empty |
| `[` | push ("", 2) | empty | ("", 2) |
| `a` | append | `a` | ("", 2) |
| `3` | count so far 3 | `a` | ("", 2) |
| `[` | push ("a", 3) | empty | ("", 2) ("a", 3) |
| `b` | append | `b` | ("", 2) ("a", 3) |
| `c` | append | `bc` | ("", 2) ("a", 3) |
| `]` | pop, current becomes `abcbcbc` | `abcbcbc` | ("", 2) |
| `d` | append | `abcbcbcd` | ("", 2) |
| `]` | pop, current becomes `abcbcbcdabcbcbcd` | `abcbcbcdabcbcbcd` | empty |

The output can be far longer than the input (`9[9[9[9[a]]]]` expands to 6561 letters), so the cost is the length of the output, not of the input.

### 7.2 Simplify path (LeetCode 71)

Cut the path at each `/`. An empty part and `.` are skipped, `..` pops (and does nothing at the root), anything else is a directory name and is pushed. The result is `/` followed by the names joined by `/`, or just `/` when the stack is empty. A part such as `...` is an ordinary name.

```cpp
// 394: a stack of (text before the bracket, repeat count).
std::string decode(const std::string& s) {
    struct Frame { std::string before; int times; };
    std::vector<Frame> st;
    std::string cur;
    int k = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') k = k * 10 + (c - '0');
        else if (c == '[') { st.push_back({cur, k}); cur.clear(); k = 0; }
        else if (c == ']') {
            Frame f = std::move(st.back());
            st.pop_back();
            for (int i = 0; i < f.times; ++i) f.before += cur;
            cur = std::move(f.before);
        } else cur.push_back(c);
    }
    return cur;
}

// 71: a stack of names; ".." pops, "." and empty parts are skipped.
std::string simplifyPath(const std::string& path) {
    std::vector<std::string> st;
    size_t i = 0, n = path.size();
    while (i < n) {
        while (i < n && path[i] == '/') ++i;
        size_t j = i;
        while (j < n && path[j] != '/') ++j;
        std::string part = path.substr(i, j - i);
        i = j;
        if (part.empty() || part == ".") continue;
        if (part == "..") { if (!st.empty()) st.pop_back(); }
        else st.push_back(part);
    }
    std::string out;
    for (const std::string& p : st) out += "/" + p;
    return out.empty() ? "/" : out;
}

// 735: positive moves right, negative moves left; equal sizes destroy each other.
std::vector<int> asteroidCollision(const std::vector<int>& a) {
    std::vector<int> st;
    for (int x : a) {
        bool alive = true;
        while (alive && x < 0 && !st.empty() && st.back() > 0) {
            if (st.back() < -x) st.pop_back();                      // the right-mover dies
            else { if (st.back() == -x) st.pop_back(); alive = false; }
        }
        if (alive) st.push_back(x);
    }
    return st;
}

// 946: can this pop order come from pushing in the given order? (distinct values, equal lengths)
bool validSequences(const std::vector<int>& pushed, const std::vector<int>& popped) {
    std::vector<int> st;
    size_t j = 0;
    for (int x : pushed) {
        st.push_back(x);
        while (!st.empty() && j < popped.size() && st.back() == popped[j]) { st.pop_back(); ++j; }
    }
    return st.empty();
}

// 844: '#' is a backspace.
std::string typed(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '#') { if (!r.empty()) r.pop_back(); }
        else r.push_back(c);
    }
    return r;
}

// The same comparison in O(1) extra space: walk both strings from the end.
bool backspaceEqualFast(const std::string& a, const std::string& b) {
    auto next = [](const std::string& s, int p) {    // last surviving index at or before p, or -1
        int skip = 0;
        for (; p >= 0; --p) {
            if (s[p] == '#') ++skip;
            else if (skip > 0) --skip;
            else return p;
        }
        return -1;
    };
    int i = (int)a.size() - 1, j = (int)b.size() - 1;
    for (;;) {
        i = next(a, i);
        j = next(b, j);
        if (i < 0 || j < 0) return i < 0 && j < 0;
        if (a[i] != b[j]) return false;
        --i;
        --j;
    }
}

// 1047: cancel equal neighbours.
std::string removeDuplicates(const std::string& s) {
    std::string st;
    for (char c : s) {
        if (!st.empty() && st.back() == c) st.pop_back();
        else st.push_back(c);
    }
    return st;
}
```

The listing above also holds the four simulations of the next section. `decode("3[a2[c]]")` returns `accaccacc`, and `simplifyPath("/a/./b/../../c/d/")` returns `/c/d`. Both were tested against generators that know the answer (30,000 nested repeat strings, 100,000 random paths against a right-to-left model with a skip counter).

---

## 8. Stacks that simulate

**Asteroid collision (LeetCode 735).** Positive numbers move right, negative move left, equal sizes destroy each other. Only a right-mover on top of the stack meeting a left-mover can collide. Keep popping smaller right-movers; if the top is equal both die; if the top is bigger the newcomer dies.

| Asteroid | Collisions | Stack |
|---|---|---|
| 4 | no collision | `[4]` |
| 7 | no collision | `[4, 7]` |
| -3 | -3 destroyed | `[4, 7]` |
| -9 | 7 destroyed; 4 destroyed | `[-9]` |
| 2 | no collision | `[-9, 2]` |
| -2 | both destroyed | `[-9]` |
| 1 | no collision | `[-9, 1]` |

The result is `[-9, 1]`: the `-9` crushed the `7` and the `4`, the `-2` cancelled the `2`. It was compared with a simulation that resolves the first adjacent collision it finds, repeatedly, on every array of length up to 7 over `{-3, -2, -1, 1, 2, 3}` and on 50,000 random arrays.

**Validate stack sequences (LeetCode 946).** Push the elements in order; after every push pop while the top equals the next wanted element. The order is possible exactly when the stack is empty at the end. This was checked against the list of all pop orders that a push/pop search can produce, for every permutation of up to 7 elements.

**Backspace string compare (LeetCode 844).** `typed` builds the final text with a stack; the comparison is then `typed(a) == typed(b)`. For O(1) extra space, walk both strings **from the end** with a skip counter: a `#` adds one to the skip, a normal character with skip above zero is erased, and the first character with skip zero is the next survivor. Both versions match a repeated-erase model on every pair of strings over `ab#` with lengths up to 9 and 3.

**Remove all adjacent duplicates (LeetCode 1047).** If the top equals the new character, pop; otherwise push. The result does not depend on the order in which pairs are removed. The stack version matches a repeated-erase model on every string over `abc` up to length 10.

---

## 9. Common mistakes

- **The last number is never applied.** The input ends and nothing flushes it. Use the fake `+` of section 4.1 or a flush after the loop.
- **Operand order.** `a - b` needs `a` below `b`; the first pop gives `b`.
- **Floor instead of truncation** in a language where `/` floors, or an `int` that overflows in a product.
- **Tie rule.** Popping on equal precedence for `^` makes `2 ^ 3 ^ 2` equal 64 instead of 512; not popping for `-` makes `8 - 3 - 2` equal 7 instead of 3.
- **Unary minus as binary.** `2 * -3`, `(-2)` and a leading `-` all have no left operand.
- **Popping past a bracket.** The `(` must stop the pop loop, and a `(` left at the end is an error.
- **Reading `s[i]` and `s[i + 1]` at the end** of the string; the fake terminator avoids it.
- **No check for an empty stack** on malformed input, which is undefined behaviour with `std::vector::back()`.
- **Recursion depth** on untrusted input (section 6.3).

---

## 10. Questions to answer out loud

1. Why is a stack, and not a queue, the right memory for brackets? (1.1)
2. What do you push in the bracket checker, and why the closer? (2.1)
3. Why does `longestValid` start with `-1` on the stack? (2.2)
4. Why does the two-pass version need the second pass? (2.3)
5. Which operand comes off first in RPN, and what goes wrong if you swap them? (3.2)
6. Why does `3 - 6 / 4` work with a stack of terms? (4.1)
7. What does the sign stack of the bracket calculator hold? (4.2)
8. State the shunting-yard pop rule. What changes for a right-associative operator? (5.1)
9. How does the scanner tell unary minus from binary minus? (5.2)
10. Where does recursive descent keep its pending operators, and what does that cost? (6.2, 6.3)
11. When would you pick shunting-yard over recursive descent? (6.4)
12. Why can the output of decode string be much longer than the input? (7.1)

---

## 11. Problems

I could not open LeetCode from here, so the statements below are summarised from memory and from the pages in Sources: read the real statement before you code.

### 11.1 On paper

**Problem A. Infix to postfix by hand.**
*Where:* paper, using the rules of section 5.1.
*Goal:* feel the tie rule for `^`.
*How:* convert `1 + 2 * 3 ^ 2 ^ 2 - 4`. After every token write the output and the operator stack. Then evaluate the postfix with a stack of numbers. How many operators are on the stack at the most?

**Problem B. The bracket calculator.**
*Where:* paper, using `calc1`.
*Goal:* watch the group signs.
*How:* the input is `2-(5-(6-7))`. After every `-`, `(` and `)` write the result, the sign and the stack of group signs. What is the answer, and how deep does the stack get?

**Problem C. The longest valid run.**
*Where:* paper, using `longestValid`.
*Goal:* see the base and the walls.
*How:* the input is `)(()())(`. For every position write the stack and the best length so far. Which positions form the longest run?

**Problem D. The frames.**
*Where:* paper, using `decode`.
*Goal:* see what a frame saves.
*How:* decode `2[ab3[c]]d`. What is on the stack, and what is the current text, at the moment the first `]` is read? What is the result?

**Problem E. Three small bugs.**
*Where:* paper, then in your editor if you want to confirm.
*Goal:* know what each rule protects.
*How:* (a) In `shunt` make `^` pop on equal precedence like the other operators (drop the test `t.kind != '^'`). What does `2^3^2` give? (b) In `calc2` run the loop only for `i < s.size()` (no fake `+` at the end). What does `3+2*2` give? (c) In `evalRPN` swap the two operands (`a` is popped first). What do `3 4 -` and `8 2 /` give?

### 11.2 In your editor

**Problem 1. LeetCode 20, 1249 and 32.**
*Where:* LeetCode, then your own tests.
*Goal:* the three bracket problems.
*How:* sections 2.1 to 2.3. Test `isBalanced` against "remove `()`, `[]`, `{}` until nothing changes" on every string over the six brackets up to length 8. Test the longest valid run against an O(n^2) scan. For 1249 test the properties (valid, a subsequence, as long as possible).

**Problem 2. LeetCode 150 and 227.**
*Where:* LeetCode.
*Goal:* RPN evaluation and the calculator without brackets.
*How:* sections 3 and 4.1. Write the stack of terms first, then the one-variable version, and compare them on random expressions with spaces and multi-digit numbers. Use a language with truncating division for your checker or write the truncation yourself.

**Problem 3. LeetCode 224, then 772.**
*Where:* LeetCode (772 is a premium problem).
*Goal:* brackets with `+` and `-`, then all four operators with brackets.
*How:* section 4.2 for 224. For 772 as I remember it (non-negative integers, `+ - * /`, brackets, truncating division) call the shunting-yard evaluator of section 5. Write it first with recursive descent and then with `shunt`, and compare.

**Problem 4. LeetCode 394 and 71.**
*Where:* LeetCode.
*Goal:* stacks of frames and of names.
*How:* sections 7.1 and 7.2. For 394 generate the input from a tree that also builds the expected output, as the tests here do. For 71 write a second version that scans the parts from the right with a skip counter and compare.

**Problem 5. Two parsers, one fuzzer.**
*Where:* your editor.
*Goal:* trust two parsers because they agree.
*How:* write `+ - * / ^` with unary minus both as shunting-yard and as recursive descent, with exceptions for bad input. Generate random strings over `0123 +-*/^()` and require the same verdict and the same value. Then add the operator `%` (remainder, same level as `*`): how many places change in each parser?

**Problem 6. The depth limit.**
*Where:* your editor and a shell.
*Goal:* measure what section 6.3 claims.
*How:* build a string of `n` open brackets, a `1`, and `n` close brackets. Find the `n` at which your recursive parser crashes (`ulimit -s` tells you the stack size), then run the explicit-stack version on a million. Then try `ulimit -s` smaller and larger and see how the crash depth moves.

**More practice.**

- **LeetCode 301, Remove Invalid Parentheses.** Section 2.4 gives the search; write it again from the counts, then write the breadth-first version (remove one bracket at a time, level by level, stop at the first level that has a valid string) and compare.
- **LeetCode 735, 946, 844, 1047.** Section 8 gives the stack for each; write them before you read the listing, and for 844 do the O(1)-space walk from the end.

---

## Answers

**Problem A.**

| Token | Action | Output | Operator stack |
|---|---|---|---|
| `1` | number to the output | `1` | empty |
| `+` | nothing to pop, push | `1` | `+` |
| `2` | number to the output | `1 2` | `+` |
| `*` | nothing to pop, push | `1 2` | `+ *` |
| `3` | number to the output | `1 2 3` | `+ *` |
| `^` | nothing to pop, push | `1 2 3` | `+ * ^` |
| `2` | number to the output | `1 2 3 2` | `+ * ^` |
| `^` | nothing to pop, push | `1 2 3 2` | `+ * ^ ^` |
| `2` | number to the output | `1 2 3 2 2` | `+ * ^ ^` |
| `-` | pop `^` `^` `*` `+`, then push | `1 2 3 2 2 ^ ^ * +` | `-` |
| `4` | number to the output | `1 2 3 2 2 ^ ^ * + 4` | `-` |
| end | pop everything | `1 2 3 2 2 ^ ^ * + 4 -` | empty |

The postfix form is `1 2 3 2 2 ^ ^ * + 4 -` and the value is 159: `3 ^ 2 ^ 2` is `3 ^ 4 = 81`, then `2 * 81 = 162`, then `1 + 162 - 4 = 159`. The two `^` signs sit on the stack together because the second `^` does not pop the first, so with `+` and `*` there are four operators on the stack at the most.

**Problem B.**

| Char | Action | Result | Sign | Group signs |
|---|---|---|---|---|
| `-` | add 2 to the result | 2 | -1 | `[1]` |
| `(` | push group sign -1 | 2 | 1 | `[1, -1]` |
| `-` | add -5 to the result | -3 | -1 | `[1, -1]` |
| `(` | push group sign 1 | -3 | 1 | `[1, -1, 1]` |
| `-` | add 6 to the result | 3 | -1 | `[1, -1, 1]` |
| `)` | add -7 to the result, pop the group | -4 | -1 | `[1, -1]` |
| `)` | add 0 to the result, pop the group | -4 | -1 | `[1]` |
| end | add 0 to the result | -4 | 1 | `[1]` |

The answer is `-4`. The stack holds three signs at the deepest point: the base `1`, then `-1` for the group after `2-`, then `1` for the group after `5-` (a minus inside a minus is a plus).

**Problem C.**

| i | Char | Action | Stack | Best |
|---|---|---|---|---|
| 0 | `)` | pop, empty, so wall: push 0 | `[0]` | 0 |
| 1 | `(` | push | `[0, 1]` | 0 |
| 2 | `(` | push | `[0, 1, 2]` | 0 |
| 3 | `)` | pop, run of length 3 - 1 = 2 | `[0, 1]` | 2 |
| 4 | `(` | push | `[0, 1, 4]` | 2 |
| 5 | `)` | pop, run of length 5 - 1 = 4 | `[0, 1]` | 4 |
| 6 | `)` | pop, run of length 6 - 0 = 6 | `[0]` | 6 |
| 7 | `(` | push | `[0, 7]` | 6 |

The longest run is 6 long: positions 1 to 6, `(()())`. Position 0 is a wall (the first `)` empties the stack) and position 7 is a `(` that is never closed; the run lies between them.

**Problem D.**

At the first `]` the stack holds `("", 2)` and `("ab", 3)` and the current text is `c`. Popping gives `ab` plus three copies of `c`, which is `abccc`. The second `]` pops `("", 2)` and gives `abcccabccc`, and the final `d` makes `abcccabcccd`. The stack is never deeper than two frames.

**Problem E.**

(a) `2^3^2` gives `64` instead of 512: the first `^` is popped before the second is read, so the result is `(2^3)^2`. (b) `3+2*2` gives `5` instead of 7: the final `2` is never multiplied into the top term, because nothing flushes it. (c) `3 4 -` gives `1` instead of -1, and `8 2 /` gives `0` instead of 4. These were run on the changed listings.

**Problem 1.** `([)]` is not balanced and `{[]}` is. `lee(t(c)o)de` is the result of `minRemoveToValid("lee(t(c)o)de)")`, and `minRemoveToValid("))((")` is empty. `longestValid(")()())")` is 4; `longestValid("(()")` is 2 (the stack version and both passes of the counter version agree).

**Problem 2.** `evalRPN({"4", "13", "5", "/", "+"})` is 6 (`13 / 5` is 2). `calc2(" 3+5 / 2 ")` is 5 and `calc2("14-3/2")` is 13.

**Problem 3.** `calc1("(1+(4+5+2)-3)+(6+8)")` is 23. `evalInfix("2*(5+5*2)/3+(6/2+8)")` is 21 (`2 * 15 = 30`, `30 / 3 = 10`, plus `3 + 8`). A recursive-descent version with the grammar of section 6.1 minus `^` and unary minus gives the same.

**Problem 4.** `decode("3[a]2[bc]")` is `aaabcbc`. `decode("2[abc]3[cd]ef")` is `abcabccdcdcdef`. `simplifyPath("/home//foo/")` is `/home/foo` and `simplifyPath("/../")` is `/`.

**Problem 5.** The verdicts must agree on malformed strings too: `2 3`, `(2`, `2)`, `1+`, `()` and `--` are all rejected by both. For `%` four places change: the list of characters in `tokenize`, `prec` (shunting-yard only), `apply` (shared) and the `term` loop (recursive descent only). `shunt` itself does not change. The two parsers still agree on the fuzz after this edit, which was run. If you needed more edits than that, your parser has the precedence knowledge in too many places.

**Problem 6.** The answer depends on the machine and on the frame size of your compiler. In the tests for this tutorial (8 MiB stack, GCC 13.3 `-O2`) 50,000 pairs worked and 70,000 crashed. A smaller `ulimit -s` moves the crash to a smaller `n` in proportion; raising it only moves the problem, which is why untrusted input needs a depth limit or the explicit stack.

**More practice.** For `()())()` the level-by-level version stops at the first level with a valid string, which is one removal, and returns the same two strings as section 2.4. In 844, `ab##` and `c#d#` are both empty, so the answer is true.

---

## Cheat sheet

- Nested means a stack: the thing opened last is finished first.
- Brackets: push the closer you need; fail on a mismatch, on an empty stack, or on a leftover.
- Longest valid run: stack of positions with a base of `-1`, or two counters scanned in both directions.
- Postfix: pop the right operand first; truncate division toward zero.
- Calculator without brackets: stack of terms, or `total` and `last`; flush the last number with a fake `+`.
- Calculator with brackets and `+ -`: stack of group signs.
- Shunting-yard: pop while `prec(top) > prec(op)`, or equal and `op` is left-associative; `^` is right-associative; a prefix operator pops nothing; `(` is a barrier.
- Unary minus: a `-` where an operand is due.
- Recursive descent: one function per precedence level, a loop for left associativity, recursion for right; the call stack is the operator stack.
- Depth: recursion for untrusted input needs a limit.
- Decode string: frame of (text before, count); simplify path: names, `..` pops.
- Simulations: pop while the newcomer wins; the result is what is left.

---

## Sources

- Titles and numbers of the LeetCode problems were checked against two pages that list them: [books.halfrost.com, Stack chapter](https://books.halfrost.com/leetcode/en/ChapterTwo/Stack/) and [dev.to, "50 Stacks LeetCode questions"](https://dev.to/harshm03/50-stacks-leetcode-questions-2llc). The second page's difficulty headings disagree with themselves on several problems, so difficulty labels are not used here.
- Sedgewick and Wayne, *Algorithms*, section 1.3: Dijkstra's two-stack evaluation (further reading, not opened here).
- All other claims in this tutorial come from the tests described at the top.
