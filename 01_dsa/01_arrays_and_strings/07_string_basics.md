# DSA 01.07: String Basics

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1.5 reading, 2.5 problems) · You need: tutorials [01](01_complexity_and_arrays_in_memory.md) (arrays in memory, amortised cost) and [02](02_two_pointers.md) (read and write indices, in-place work) · Companion code: [`07_string_basics_demo.cpp`](07_string_basics_demo.cpp)*

By the end of this tutorial you can:

- say what `size()`, `char`, `std::string`, `std::string_view` and a C string really are, and what each operation costs;
- compare strings by their characters (anagrams, groups of anagrams) with a counter array or a canonical key;
- find palindromic substrings by growing outwards from every centre;
- parse numbers and fields out of text without overflow or off-by-one mistakes (`atoi`, split, version compare, reverse words, length-prefixed encoding);
- add and multiply numbers of any length stored as digit strings;
- build long strings in linear time, and explain why `s = s + c` in a loop is quadratic;
- write `memmove`, and say why `strncpy` is not a safe `strcpy`.

Every function in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined` and checks each function against a brute force on random inputs.

---

## 1. What a string is in C++

### 1.1 Bytes, not characters

A `std::string` is a resizable array of `char`, a one-byte integer. `s.size()` is the number of **bytes**, `s[i]` is byte `i`, and both are O(1). Everything in this tutorial works on bytes. That is right for ASCII and wrong for accented letters, other scripts and emoji unless you decode first.

UTF-8 stores one character in 1 to 4 bytes. Every byte after the first of a multi-byte character looks like `10xxxxxx`, so counting the bytes that do **not** look like that counts characters:

```cpp
// How many characters (code points) does a UTF-8 string hold? s.size() counts bytes.
// Every byte is either a start byte or a continuation byte of the form 10xxxxxx; count the start bytes.
int utf8Length(const std::string& s) {
    int count = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++count;                      // not a continuation byte: a new character starts here
    return count;
}
```

| Text | `size()` | Characters |
|---|---|---|
| `hello` | 5 | 5 |
| `héllo` | 6 | 5 |
| `日本語` | 9 | 3 |
| `😀` | 4 | 1 |

In an interview, ask "ASCII only? lowercase letters only?" before you design anything. The answer chooses between `count[26]`, `count[256]` and a hash map.

### 1.2 `char` is a small integer

- `'c' - 'a'` is an `int` with value 2. That is how a lowercase letter becomes an index from 0 to 25. A digit character becomes its value with `s[i] - '0'`.
- Whether plain `char` is signed depends on the platform: x86 compilers make it signed, many ARM Linux compilers make it unsigned. A byte above 127 can therefore be a **negative** `char`, and `count[c]` becomes a negative index. Cast to `unsigned char` first. The same rule holds for `std::isdigit`, `std::tolower` and the other `<cctype>` functions: they are defined only for values of `unsigned char`.

### 1.3 Owning and viewing

| Type | Owns its bytes? | Notes |
|---|---|---|
| `std::string` | yes | copying copies the bytes; short strings live inside the object (the small-string optimisation) |
| `std::string_view` | no | a pointer and a length (16 bytes); slicing and passing never copy; dangling if the owner is destroyed or changed |
| `const char*` | no | a pointer; the length is found by scanning for `'\0'`, which is O(n) |

For read-only parameters, take `const std::string&` or `std::string_view`. Return `std::string` by value: moves are free. The classic bug is a view of a temporary: `std::string_view v = std::string("abc");` leaves `v` pointing at memory that has already been freed.

The companion prints `sizeof(std::string)` and the capacity of an empty string. With libstdc++ (GCC) they are 32 and 15; Apple's libc++ uses 24 and 22. Never depend on these numbers; the point is that short strings need no heap allocation.

### 1.4 What the operations cost

Let `n` and `m` be the lengths of `s` and `t`.

| Operation | Cost |
|---|---|
| `s.size()`, `s[i]`, `s.back()` | O(1) |
| `s.push_back(c)`, `s += c` | amortised O(1) |
| `s == t`, `s < t` | O(min(n, m)); `==` is O(1) when the sizes differ |
| `s + t`, `s.substr(i, k)`, copying `s` | O(n + m), O(k), O(n) |
| `s.insert(0, c)`, `s.erase(0, 1)` | O(n): everything shifts |
| `s.find(t)` | O(n × m) worst case (linear-time search is in the Strings area) |
| using `s` as a hash-map key | O(n) per lookup: the whole string is hashed and compared |

The last row matters: a hash map of strings is not O(1) per operation, it is O(length of the key).

---

## 2. Counting characters

### 2.1 Anagrams (LeetCode 242)

Two strings are anagrams if they use the same characters the same number of times. Sorting both and comparing costs O(n log n). Counting costs O(n): add one for every character of `s`, subtract one for every character of `t`, and fail as soon as a counter goes below zero.

```cpp
// LeetCode 242: do s and t use exactly the same characters, the same number of times?
bool isAnagram(const std::string& s, const std::string& t) {
    if (s.size() != t.size()) return false;                   // different lengths can never match
    int count[256] = {0};                                     // one counter per possible byte value
    for (unsigned char c : s) ++count[c];
    for (unsigned char c : t)
        if (--count[c] < 0) return false;                     // t has more of this character than s does
    return true;
}
```

If the lengths are equal, "no counter ever went negative" is enough: `t` can take at most as many of each letter as `s` has, the totals are equal, so every counter ends at exactly zero. That is why the length check comes first.

Trace for `s = "aab"`, `t = "abb"`:

| Step | Counters | Result |
|---|---|---|
| after counting `s` | `a: 2, b: 1` | |
| `t[0] = a` | `a: 1` | continue |
| `t[1] = b` | `b: 0` | continue |
| `t[2] = b` | `b: -1` | **false** |

`count[256]` indexed by `unsigned char` handles any byte. For lowercase letters only, `int count[26]` with `c - 'a'` is smaller; for Unicode text, decode to code points and use a hash map.

### 2.2 First unique character (LeetCode 387)

Two passes: count everything, then find the first character whose count is 1.

```cpp
// LeetCode 387: the index of the first character that occurs exactly once, or -1.
int firstUniqChar(const std::string& s) {
    int count[256] = {0};
    for (unsigned char c : s) ++count[c];                     // pass 1: count everything
    for (size_t i = 0; i < s.size(); ++i)
        if (count[(unsigned char)s[i]] == 1) return (int)i;   // pass 2: the first character that is unique
    return -1;
}
```

`"leetcode"` gives 0, `"loveleetcode"` gives 2 (the `v`), `"aabb"` gives -1. O(n) time, O(1) space (the counter array has a fixed size).

### 2.3 Grouping by a canonical form (LeetCode 49)

To group anagrams, give every word a **key** that is the same for all anagrams of it and different for everything else. Two good keys: the word with its letters sorted, or the 26 letter counts written as text.

The code uses `unordered_map`, a hash map from a key to a value with expected O(1) cost per operation (the Hashing area covers it). `groups[key]` finds the entry for `key`, or creates an empty one.

```cpp
// LeetCode 49, first version: the key is the word with its letters sorted.
std::vector<std::vector<std::string>> groupAnagrams(const std::vector<std::string>& words) {
    std::unordered_map<std::string, std::vector<std::string>> groups;
    for (const auto& w : words) {
        std::string key = w;
        std::sort(key.begin(), key.end());                    // anagrams have the same sorted form
        groups[key].push_back(w);
    }
    std::vector<std::vector<std::string>> result;
    for (auto& entry : groups) result.push_back(std::move(entry.second));
    return result;
}
```

Keys for `{"eat", "tea", "tan", "ate", "nat", "bat"}`:

| Word | Sorted key |
|---|---|
| eat, tea, ate | `aet` |
| tan, nat | `ant` |
| bat | `abt` |

So the groups are `[eat, tea, ate]`, `[tan, nat]`, `[bat]`. The order of the groups (and of the map) is not defined; compare results after sorting when you test.

The count key avoids the sort:

```cpp
// For lowercase words only: the 26 letter counts as text, so no sorting is needed.
std::string countKey(const std::string& w) {
    int count[26] = {0};
    for (char c : w) ++count[c - 'a'];
    std::string key;
    for (int c : count) {
        key += std::to_string(c);
        key += '#';                                           // the separator keeps counts 1,11 apart from 11,1
    }
    return key;
}

std::vector<std::vector<std::string>> groupAnagramsByCount(const std::vector<std::string>& words) {
    std::unordered_map<std::string, std::vector<std::string>> groups;
    for (const auto& w : words) groups[countKey(w)].push_back(w);
    std::vector<std::vector<std::string>> result;
    for (auto& entry : groups) result.push_back(std::move(entry.second));
    return result;
}
```

For `"aab"` the key starts `2#1#` followed by twenty-four more `0#`. The `'#'` is not decoration. Without a separator, the counts (a = 1, b = 11) and (a = 11, b = 1) both turn into `111` followed by zeros, and two different words land in the same group. The companion checks this exact pair.

| Key | Time per word of length L | Notes |
|---|---|---|
| sorted letters | O(L log L) | simplest; works for any characters |
| letter counts | O(L + 26) | needs a small alphabet |

The idea behind both is general: **reduce each item to a canonical form so that equal things get equal keys**, then group with a map. It also solves isomorphic strings, "same shape" patterns and "same set of letters" questions.

---

## 3. Palindromes

A palindrome reads the same in both directions. Checking one string is the two-pointer loop of tutorial 02 (LeetCode 125). Finding the longest palindromic **substring** (LeetCode 5) is harder: there are n(n + 1)/2 substrings and checking each costs O(n), which is O(n³) overall.

Better: every palindrome has a **centre**, and it grows outwards from it. If `s[l] == s[r]`, the palindrome extends to `l - 1` and `r + 1`; the first mismatch (or the string's end) stops it. A palindrome of odd length is centred on a letter, one of even length on the gap between two letters, so a string of length n has n letters plus n - 1 gaps, which is **2n - 1 centres**.

```cpp
// LeetCode 5: the longest palindromic substring, by growing outwards from every possible centre.
std::string longestPalindrome(const std::string& s) {
    int n = (int)s.size(), bestStart = 0, bestLen = 0;
    for (int center = 0; center < 2 * n - 1; ++center) {      // 2n - 1 centres: n letters and n - 1 gaps between letters
        int l = center / 2, r = l + center % 2;               // a gap centre starts with l and l + 1, a letter centre with l == r
        while (l >= 0 && r < n && s[l] == s[r]) { --l; ++r; } // grow while both ends match
        int len = r - l - 1;                                  // the loop stopped one step past the palindrome on each side
        if (len > bestLen) { bestLen = len; bestStart = l + 1; }
    }
    return s.substr(bestStart, bestLen);
}
```

Centre number `c` starts at `l = c / 2` and `r = l + c % 2`: even `c` is the letter `l` (with `r == l`), odd `c` is the gap between `l` and `l + 1`. After the loop, `l` and `r` are one step **past** the palindrome on each side, so its length is `r - l - 1` and it starts at `l + 1`. The strict `len > bestLen` keeps the leftmost of several longest palindromes.

Trace for `"babad"`:

| Centre `c` | Starts at `(l, r)` | Longest palindrome there |
|---|---|---|
| 0 | (0, 0) | `b` |
| 1 | (0, 1) | (none) |
| 2 | (1, 1) | `bab` |
| 3 | (1, 2) | (none) |
| 4 | (2, 2) | `aba` |
| 5 | (2, 3) | (none) |
| 6 | (3, 3) | `a` |
| 7 | (3, 4) | (none) |
| 8 | (4, 4) | `d` |

The first longest is `bab` (length 3); `aba` has the same length but is not kept. LeetCode accepts either. Time O(n²) in the worst case (`"aaaa..."` expands every centre to the ends), space O(1).

Counting palindromic substrings (LeetCode 647) is the same loop with one change: every successful growth step is one more palindrome.

```cpp
// LeetCode 647: how many substrings are palindromes (equal substrings at different places count separately)?
int countPalindromicSubstrings(const std::string& s) {
    int n = (int)s.size(), count = 0;
    for (int center = 0; center < 2 * n - 1; ++center) {
        int l = center / 2, r = l + center % 2;
        while (l >= 0 && r < n && s[l] == s[r]) { ++count; --l; ++r; }   // every successful growth step is one more palindrome
    }
    return count;
}
```

For `"aaa"` the centres give 1, 1, 2, 1, 1: the letters `a`, `a`, `a`; the gaps `aa` (twice); and `aaa`. The total is **6**. Equal substrings at different positions count separately.

Alternatives: a DP table `isPal[i][j]` is also O(n²) time but O(n²) space. **Manacher's algorithm** finds all palindromes in O(n) by reusing earlier expansions; it is in the Strings area (tutorial 03). For n up to a few thousand, expanding around centres is the right answer in an interview.

---

## 4. Parsing text into values

Parsing problems are won by listing the cases before writing code: empty input, only spaces, a sign with no digits, a number followed by letters, numbers too large for the type.

### 4.1 String to integer (LeetCode 8)

The rules: skip leading spaces; read an optional `+` or `-`; read digits until the first other character; clamp the result to the range of a 32-bit `int`.

```cpp
// LeetCode 8: skip spaces, read an optional sign, read digits, clamp to the range of int.
int myAtoi(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n && s[i] == ' ') ++i;                         // 1. leading spaces
    int sign = 1;
    if (i < n && (s[i] == '+' || s[i] == '-')) sign = (s[i++] == '-') ? -1 : 1;   // 2. at most one sign
    int result = 0;
    for (; i < n && std::isdigit((unsigned char)s[i]); ++i) { // 3. digits, until the first other character
        int d = s[i] - '0';
        if (result > (INT_MAX - d) / 10)                      // result * 10 + d would not fit in an int: clamp
            return sign == 1 ? INT_MAX : INT_MIN;
        result = result * 10 + d;
    }
    return sign * result;
}
```

The one subtle line is the overflow test. We want to know whether `result * 10 + d` would exceed `INT_MAX`, **without computing it** (computing it in an `int` is already undefined behaviour). Rearranged:

`result * 10 + d <= INT_MAX`  ⇔  `result <= (INT_MAX - d) / 10`

with integer division, because `result` is an integer. So we clamp when `result > (INT_MAX - d) / 10`. With `INT_MAX = 2147483647` the bound is 214748364 for `d <= 7` and 214748363 for `d = 8` or `9`.

Traces:

| Input | What happens | Result |
|---|---|---|
| `"   -042"` | skip 3 spaces, sign `-`, digits give `0`, `4`, `42` | -42 |
| `"-91283472332"` | `result` reaches 912834723 after 9 digits; at the 10th digit 912834723 > 214748364 | `INT_MIN` |
| `"2147483647"` | `result` reaches 214748364, then 214748364 > 214748364 is false | 2147483647 |
| `"2147483648"` | at the last digit 214748364 > 214748363 | `INT_MAX` |
| `"-2147483648"` | the same test trips, the sign is `-` | `INT_MIN` |

The last row is a pleasant accident: the magnitude 2147483648 does not fit in an `int`, so the code clamps, and the clamped value `INT_MIN` is exactly the right answer.

Cases to try:

| Input | Result | Why |
|---|---|---|
| `""`, `"   "`, `"+"`, `"-"` | 0 | no digits |
| `"+-12"` | 0 | a second sign is not a digit |
| `"12abc"` | 12 | stop at the first non-digit |
| `"abc12"` | 0 | the first character is not a space, sign or digit |
| `"0012"` | 12 | leading zeros add nothing |
| `"  +  413"` | 0 | spaces after the sign are not allowed |

In real code do not write this by hand. `std::from_chars` (C++17) parses without allocation or locale and returns an error code; `std::stoi` throws on bad input; `strtol` reports where it stopped and sets `errno`; plain `atoi` cannot report an error at all and has undefined behaviour on overflow. LeetCode's `atoi` clamps and ignores trailing text, which a parser for untrusted input must not do: it should reject.

The inverse, an `int` as text, has its own trap. `INT_MIN` has no positive counterpart in an `int`, so negating it is undefined. Widen first:

```cpp
// The inverse: an int as text. INT_MIN has no positive counterpart in an int, so work in long long.
std::string intToString(int value) {
    if (value == 0) return "0";
    long long v = value;
    bool negative = v < 0;
    if (negative) v = -v;
    std::string out;
    while (v > 0) {
        out.push_back(char('0' + v % 10));                    // the last digit first ...
        v /= 10;
    }
    if (negative) out.push_back('-');
    std::reverse(out.begin(), out.end());                     // ... so reverse at the end
    return out;
}
```

For `507` the loop produces `7`, `0`, `5`, and the reverse gives `507`. Building the digits backwards and reversing once is the standard way to avoid inserting at the front (section 6).

### 4.2 Split and join

Splitting looks easy and has one decision: **do empty fields count?** In `"a,,b,"` there are four fields: `a`, empty, `b`, empty. Keeping them is the safe default (a CSV line with an empty column must stay aligned). The `find` loop below keeps them, and `join` is its exact inverse.

```cpp
// Splits on every delimiter and keeps the empty fields: "a,,b," gives {"a", "", "b", ""}.
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {                       // no more delimiters: the rest is the last field
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// Joins with a separator. The size is computed first so the string allocates once.
std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    if (parts.empty()) return "";
    size_t total = sep.size() * (parts.size() - 1);
    for (const auto& p : parts) total += p.size();
    std::string out;
    out.reserve(total);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}
```

Trace of `split("a,,b,", ',')`:

| `start` | `find` returns | Field pushed |
|---|---|---|
| 0 | 1 | `a` |
| 2 | 2 | (empty) |
| 3 | 4 | `b` |
| 5 | not found | (empty): the rest of the string |

Other tools drop fields without telling you: a `getline` loop loses a trailing empty field, `istringstream >> word` skips all empty ones, and `strtok` treats runs of delimiters as one. Know which one you are using.

`join` computes the total length first and calls `reserve`, so the result is built with a single allocation (section 6).

`split` copies every piece. When the pieces are only read and the original text outlives them, return views instead:

```cpp
// Splits without copying: every piece is a view into the original text, which must outlive the views.
std::vector<std::string_view> splitView(std::string_view s, char delim) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));                 // a view: no allocation, no copy
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}
```

Each `string_view` points into `s`; nothing is allocated except the vector. The views are valid only while `s` is alive and unchanged.

### 4.3 Compare version numbers (LeetCode 165)

`"1.01"` equals `"1.001"`, `"1.0"` equals `"1.0.0"`, and `"1.10"` is larger than `"1.2"`. A string comparison gets all three wrong. Compare revision by revision as **numbers**, treating a missing revision as 0. You do not need to split first; read one revision from each string per round:

```cpp
// LeetCode 165: compare dotted version numbers; -1, 0 or 1. A missing revision counts as 0.
int compareVersion(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') x = x * 10 + (a[i++] - '0');   // the next revision of a (leading zeros vanish)
        while (j < b.size() && b[j] != '.') y = y * 10 + (b[j++] - '0');   // the next revision of b
        if (x != y) return x < y ? -1 : 1;
        ++i;                                                  // step over the dots
        ++j;
    }
    return 0;
}
```

Trace:

| `a` | `b` | Revision pairs `(x, y)` | Result |
|---|---|---|---|
| `1.01` | `1.001` | (1, 1), (1, 1) | 0 |
| `1.0` | `1.0.0` | (1, 1), (0, 0), (0, 0) | 0 |
| `7.5.2.4` | `7.5.3` | (7, 7), (5, 5), (2, 3) | -1 |
| `1.0.1` | `1` | (1, 1), (0, 0), (1, 0) | 1 |

When a string runs out, its inner loop does not run, so `x` or `y` stays 0, which is exactly the "missing revision is 0" rule. The `++i; ++j` at the end steps over the dots (and harmlessly past the end).

### 4.4 Reverse the words (LeetCode 151)

Reverse the order of the words, removing extra spaces. The in-place trick: reverse the **whole** string first, so the words are in the right order but each one is backwards; then walk through it, copying each word down to the write position (squeezing out spaces) and reversing that word back.

```cpp
// LeetCode 151: reverse the order of the words; drop extra spaces. Reverse everything, then each word.
std::string reverseWords(std::string s) {
    std::reverse(s.begin(), s.end());                         // words are now in reverse order, each one backwards
    size_t write = 0, n = s.size();
    for (size_t read = 0; read < n; ++read) {
        if (s[read] == ' ') continue;
        if (write != 0) s[write++] = ' ';                     // exactly one space between words
        size_t start = write;
        while (read < n && s[read] != ' ') s[write++] = s[read++];   // copy one word down
        std::reverse(s.begin() + start, s.begin() + write);   // and turn it the right way round
    }
    s.resize(write);
    return s;
}
```

For `"  hello world  "`:

| Step | `s[0..write)` |
|---|---|
| after reversing everything | the whole string is `"  dlrow olleh  "` |
| first word copied and reversed | `world` (`write` = 5) |
| one space, second word copied and reversed | `world hello` (`write` = 11) |
| `resize(11)` | `world hello` |

`write` never passes `read`: it advances once per character copied, plus one space per word, and each space it adds corresponds to at least one space that `read` has already skipped. So the copy never overwrites a character that has not been read yet. O(n) time, O(1) extra space.

### 4.5 Encode and decode a list of strings (LeetCode 271)

Pack a list of strings into one string and recover it exactly. A delimiter alone fails, because a string can contain the delimiter. Write the **length first**: the decoder reads digits up to the first `#`, learns `len`, takes exactly `len` bytes, and jumps over them. A `#` inside the payload is never looked at.

```cpp
// LeetCode 271: pack a list of strings into one string and get them back, whatever characters they contain.
std::string encodeStrings(const std::vector<std::string>& strs) {
    std::string out;
    for (const auto& s : strs) out += std::to_string(s.size()) + '#' + s;   // the length, a separator, then the bytes
    return out;
}

std::vector<std::string> decodeStrings(const std::string& data) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < data.size()) {
        size_t hash = data.find('#', i);                      // the first '#' after a length is always the separator
        size_t len = std::stoul(data.substr(i, hash - i));
        out.push_back(data.substr(hash + 1, len));
        i = hash + 1 + len;                                   // jump over the payload: a '#' inside it is never looked at
    }
    return out;
}
```

`{"ab", "", "c#d"}` becomes `2#ab0#3#c#d`. Decoding: at position 0, the first `#` is at 1, so the length is 2 and the payload is `ab`; at position 4 the length is 0, so the payload is empty; at position 6 the length is 3, so the payload is `c#d`.

This is the TLV idea (type, length, value) behind many binary formats and protocols: Wi-Fi information elements, TCP options, ASN.1, protobuf. A reader that trusts the length field must check it against the bytes that are really left.

---

## 5. Big numbers as strings

When numbers do not fit in 64 bits, keep them as digit strings and do arithmetic the way you learned in school.

### 5.1 Add (LeetCode 415)

Walk both strings from the right, add the digits and the carry, write the last digit of the sum, keep the rest as the next carry. Continue while either string has digits **or a carry remains**; the carry is what turns `999 + 1` into `1000`.

```cpp
// LeetCode 415: the sum of two non-negative integers given as digit strings.
std::string addStrings(const std::string& a, const std::string& b) {
    std::string out;
    int i = (int)a.size() - 1, j = (int)b.size() - 1, carry = 0;
    while (i >= 0 || j >= 0 || carry) {
        int sum = carry;
        if (i >= 0) sum += a[i--] - '0';
        if (j >= 0) sum += b[j--] - '0';
        out.push_back(char('0' + sum % 10));                  // least significant digit first
        carry = sum / 10;
    }
    std::reverse(out.begin(), out.end());
    return out.empty() ? "0" : out;                           // two empty inputs
}
```

For `456 + 77`:

| Column | Sum (digits + carry in) | Digit written | Carry out |
|---|---|---|---|
| units | 6 + 7 = 13 | 3 | 1 |
| tens | 5 + 7 + 1 = 13 | 3 | 1 |
| hundreds | 4 + 0 + 1 = 5 | 5 | 0 |

The digits come out as `3`, `3`, `5`; the reverse is `533`.

### 5.2 Multiply (LeetCode 43)

Schoolbook multiplication multiplies every digit of `a` by every digit of `b`. Digit `i` of `a` (counted from the left) has weight `10^(la - 1 - i)`, digit `j` of `b` has weight `10^(lb - 1 - j)`, so their product has weight `10^(la + lb - 2 - i - j)`. In a result array of `la + lb` cells indexed from the left, cell `p` has weight `10^(la + lb - 1 - p)`. So the product's **units** digit belongs in cell `i + j + 1` and its **carry** in cell `i + j`.

```cpp
// LeetCode 43: the product of two non-negative integers given as digit strings.
std::string multiplyStrings(const std::string& a, const std::string& b) {
    if (a == "0" || b == "0") return "0";
    std::vector<int> pos(a.size() + b.size(), 0);             // the product has at most len(a) + len(b) digits
    for (int i = (int)a.size() - 1; i >= 0; --i)
        for (int j = (int)b.size() - 1; j >= 0; --j) {
            int product = (a[i] - '0') * (b[j] - '0') + pos[i + j + 1];   // the units place for this pair is i + j + 1
            pos[i + j + 1] = product % 10;
            pos[i + j] += product / 10;                       // the carry goes one place to the left
        }
    std::string out;
    size_t k = 0;
    while (k < pos.size() && pos[k] == 0) ++k;                // skip leading zeros
    for (; k < pos.size(); ++k) out.push_back(char('0' + pos[k]));
    return out;
}
```

Trace for `23 × 45` (the array has 4 cells):

| `(i, j)` | Digits | Value (product + cell `i + j + 1`) | `pos` afterwards |
|---|---|---|---|
| (1, 1) | 3 × 5 | 15 + 0 = 15 | `[0, 0, 1, 5]` |
| (1, 0) | 3 × 4 | 12 + 1 = 13 | `[0, 1, 3, 5]` |
| (0, 1) | 2 × 5 | 10 + 3 = 13 | `[0, 2, 3, 5]` |
| (0, 0) | 2 × 4 | 8 + 2 = 10 | `[1, 0, 3, 5]` |

The result is `1035`. A cell can hold a value above 9 for a while (a carry added to it); it is reduced the next time it is used as a units cell, and when the last pair is done every cell is a single digit (the companion checks this against 128-bit arithmetic on thousands of random inputs). The zero special case and the leading-zero skip handle `"0"` and products with fewer than `la + lb` digits.

Cost O(la × lb). For 10,000-digit numbers that is 10⁸ digit operations: fine. Libraries pack many digits into one machine word (base 10⁹ in a `uint32_t`, or a full 64-bit limb in GMP) which divides the work by the square of the packing factor, and switch to Karatsuba (O(n^1.585)) or FFT-based multiplication for very large inputs. The big-integer arithmetic behind RSA is exactly this: arrays of 32 or 64-bit limbs with carries.

---

## 6. Building strings without wasting time

`s += c` appends in amortised O(1): the buffer grows geometrically (tutorial 01, section 1.7). `s = s + c` is different: it builds a **new** string of length n + 1 from a copy of `s`, then moves it into `s`. That is O(n) per step and O(n²) for a loop.

The companion counts heap allocations while building a 1000-character string three ways (by replacing the global `operator new`):

| How | Allocations (libstdc++, measured) |
|---|---|
| `s += 'x'` in a loop | 7 |
| `s.reserve(1000)`, then `s += 'x'` | 1 |
| `s = s + 'x'` in a loop | 985 |

Other libraries give slightly different numbers, not a different pattern. The rules that follow:

1. **Append with `+=`, `push_back` or `append`.** If the final size is known or easy to estimate, `reserve` it first (as `join` does).
2. **Never insert or erase at the front in a loop** (`insert(0, c)`, `erase(0, 1)`): O(n) each. Build backwards and `reverse` once, as `intToString` and `addStrings` do.
3. **Do not call `substr` in a loop** to look at pieces: every call allocates and copies. Use indices, or `string_view` slices.
4. **Pass strings by `const&` or as views.** Passing by value copies unless you move.
5. For the fastest formatting in a hot path, `snprintf` or `std::to_chars` into a fixed buffer beats `ostringstream` and `to_string` chains.

---

## 7. C strings and library functions

A C string is a `char` array ended by a zero byte. It is how the kernel, drivers, firmware and most C APIs pass text, and `std::string::c_str()` gives you one. Three habits keep you out of trouble.

**`strlen` scans the whole string.** It is O(n), so `for (i = 0; i < strlen(s); ++i)` is O(n²) unless the compiler proves `s` does not change (do not count on it). Compute the length once.

**Embedded zeros.** A `std::string` may contain `'\0'` (the companion's encode and decode test uses them); every C function stops at the first one.

**Bounded copies.** `strcpy` copies until the source ends and overflows a small destination; unbounded copies like it are behind many historical security bugs. Its "safe" sibling `strncpy` is not safe: when the source is as long as the limit or longer, it copies the bytes and does **not** write the terminating zero, and when the source is shorter it pads the rest of the buffer with zeros (slow for big buffers). A bounded copy that always terminates and tells you about truncation:

```cpp
// A bounded copy that always ends with '\0' (strncpy does not). Returns the length that was wanted,
// so the caller can detect truncation by comparing it with cap, like snprintf does.
size_t safeCopy(char* dst, size_t cap, const char* src) {
    size_t len = std::strlen(src);
    if (cap == 0) return len;
    size_t n = len < cap - 1 ? len : cap - 1;                 // leave one byte for the terminator
    std::memcpy(dst, src, n);
    dst[n] = '\0';
    return len;
}
```

`safeCopy(buf, 4, "abcdef")` stores `abc\0` and returns 6; since 6 >= 4, the caller knows the text was cut. This is what `snprintf(dst, cap, "%s", src)` does, and the companion compares the two on thousands of random inputs.

### 7.1 memcpy and memmove

`memcpy` copies `n` bytes from `src` to `dst` and requires the regions **not to overlap**. `memmove` accepts overlap and gives the result you would get if the bytes were first copied to a scratch area. The only trap is the order of copying. Shift `abcdef` right by 2 (copy 4 bytes from offset 0 to offset 2) front to back:

| Copy direction | Steps | Result |
|---|---|---|
| front to back (wrong here) | `b[2] = b[0]`, `b[3] = b[1]`, then `b[4] = b[2]` copies the **new** `a`, `b[5] = b[3]` copies the new `b` | `ababab` |
| back to front (right) | `b[5] = b[3]`, `b[4] = b[2]`, `b[3] = b[1]`, `b[2] = b[0]` | `ababcd` |

The rule: if `dst` is **below** `src`, or the regions do not overlap, copy front to back; if `dst` starts **inside** `src`, copy back to front.

```cpp
// What memmove does: copy n bytes even if the two regions overlap.
void* myMemmove(void* dst, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    std::uintptr_t da = reinterpret_cast<std::uintptr_t>(d), sa = reinterpret_cast<std::uintptr_t>(s);
    if (da == sa || n == 0) return dst;
    if (da < sa || da >= sa + n) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];           // dst is below src, or the regions do not overlap: front to back
    } else {
        for (size_t i = n; i-- > 0;) d[i] = s[i];             // dst starts inside src: copy back to front
    }
    return dst;
}
```

Shifting left (`dst` below `src`) is safe front to back: `abcdef` with 4 bytes copied from offset 2 to offset 0 gives `cdefef`. The pointers are compared as integers because comparing pointers into different objects is not defined by the language; library implementations are written for their platform. Real `memmove` implementations also copy 8, 16 or 64 bytes per step and use vector instructions (CO 21), but the direction rule is the same.

---

## 8. Where this shows up in systems

- **Length-prefixed fields.** Packet headers, Wi-Fi information elements (element ID, length, data), TCP options, TLS records and serialisation formats all use the encoding of section 4.5, because a length survives any content. The matching bug is trusting a length without checking it against the buffer: it is behind many remote-code-execution and information-leak vulnerabilities.
- **Bytes, not characters.** An 802.11 SSID is 0 to 32 **octets**, and not necessarily valid UTF-8. A length limit in the protocol is a limit on `size()`, not on `utf8Length()`. Network names, file names on Linux and MAC or IP text all want a byte-oriented design.
- **Bounded copies and formatting.** Firmware and drivers copy into fixed buffers all day. `snprintf` and a `safeCopy` style function (never `strcpy`, `sprintf`, `gets`) are the baseline; code review and fuzzing look for the rest.
- **`memmove` in buffers.** Removing a header from a packet, shifting data in a ring buffer or compacting a table all copy overlapping memory. Using `memcpy` there is a bug that appears only for some sizes.
- **Canonical forms and versions.** Flow keys, MAC address normalisation and case folding of host names are all "turn it into a canonical form, then compare or hash", the same idea as sorting the letters of a word. Firmware and package versions are compared revision by revision as numbers (section 4.3); comparing them as text is a classic release-pipeline bug.
- **Logging and allocation.** Formatting a log line with `+` and `to_string` allocates several times. In a hot path, format into a preallocated buffer; allocation time shows up in tail latency.

---

## 9. Choosing the tool

| The task | Use |
|---|---|
| Same characters in any order | counter array (`26` or `256`) or a sorted copy |
| Group items that are "the same up to rearranging" | canonical key (sorted string or count string) in a hash map |
| Longest or all palindromic substrings, n up to a few thousand | expand around the 2n - 1 centres |
| Palindromic substrings, n around 10⁶ | Manacher (Strings area) |
| Text to number | explicit steps (spaces, sign, digits) and an overflow test **before** multiplying |
| Split text into fields | `find` loop that keeps empty fields; `string_view` slices if nothing needs to own the pieces |
| Compare dotted versions | read one revision from each string per round, as numbers |
| Reverse the order of words | reverse everything, then reverse each word |
| Store several strings in one | length prefix, not a delimiter |
| Add or multiply huge numbers | digit arrays with carries (machine-word limbs in production) |
| Build a long string | `reserve`, then `+=`; never `s = s + c`, never insert at the front |
| Copy bytes that may overlap | `memmove`: front to back when `dst` is below `src`, else back to front |
| Copy into a fixed buffer | `snprintf` or a `safeCopy`, never `strcpy` or `strncpy` |

---

## 10. Mistakes that cost points

1. **Indexing a counter array, or calling `isdigit` or `tolower`, with a plain `char`.** A byte above 127 can be negative. Cast to `unsigned char`.
2. **Treating `size()` as the number of characters** in text that may be UTF-8.
3. **Anagram check without the length check,** or a `count[26]` array on input with capitals, digits or other bytes.
4. **Palindromes: only odd centres** (missing `"abba"`), or a length of `r - l + 1` after the loop (it is two too many; the loop stops one step past the palindrome on each side).
5. **`atoi`: testing for overflow after multiplying.** The overflow has already happened and is undefined for `int`. Test with `result > (INT_MAX - d) / 10` first. Also forgetting that `"+-12"` gives 0, and that the sign decides between `INT_MAX` and `INT_MIN`.
6. **Splitting that loses empty fields,** especially the last one after a trailing delimiter.
7. **Comparing versions as text** (`"1.10" < "1.2"`) or treating `1.0` and `1.0.0` as different.
8. **Multiplying digit strings:** units digit in the wrong cell, no special case for `"0"`, leading zeros left in the result.
9. **`s = s + c`, `insert(0, c)` or `substr` inside a loop.** All O(n²) over the whole loop.
10. **A view that outlives its string,** or a view of a string that is then modified.
11. **`strncpy` without a terminator,** `strcpy` into a small buffer, `strlen` in a loop condition, `memcpy` on overlapping memory.
12. **Encoding a list with a delimiter only.** Any string may contain it. Use a length prefix.

---

## 11. Questions you will be asked

**Are two strings anagrams? What does it cost?** Equal lengths, then count characters of one up and the other down; fail if a counter goes negative. O(n) time, O(1) space (a fixed-size array). Sorting both is O(n log n).

**Group anagrams: the complexity and the choice of key.** With a sorted-letters key it is O(n × L log L) for n words of length L; with a count key it is O(n × (L + 26)). Mention that hashing a key is itself proportional to the key's length.

**Longest palindromic substring: the options.** Brute force O(n³); DP table O(n²) time and space; expanding around 2n - 1 centres O(n²) time and O(1) space; Manacher O(n). Say why there are 2n - 1 centres (odd and even lengths).

**Implement `atoi`. How do you avoid overflow without a wider type?** Test `result > (INT_MAX - d) / 10` before `result * 10 + d`. Mention that production parsers report errors instead of clamping (`from_chars`, `strtol` with `errno`).

**Why is `s = s + c` in a loop slow, and what is the fix?** Each step builds and copies a new string of length n, so the loop is O(n²). `+=` is amortised O(1) because capacity grows geometrically; `reserve` removes even that.

**`std::string` or `std::string_view`? And what does `size()` count?** The string owns its bytes; the view is a pointer and a length that must not outlive the owner or see it modified. `size()` counts bytes, not characters.

**`memcpy` or `memmove`? Write `memmove`.** `memmove` when the regions may overlap. If `dst` is below `src` (or there is no overlap) copy front to back, otherwise back to front.

**Why is `strncpy` not a safe `strcpy`?** It does not terminate the result when the source fills the buffer, and it pads with zeros otherwise. Use `snprintf` or a function that always terminates and reports truncation.

**Multiply two numbers of 10,000 digits.** Schoolbook is O(n²) (10⁸ digit operations, fine). Pack digits into machine words (base 10⁹) to cut the work by about 80 times; Karatsuba or FFT beyond that.

**Reverse the words of a sentence in O(1) extra space.** Reverse the whole string, then reverse each word while compacting the spaces.

---

## 12. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). First ask: do I need to count characters, grow from a centre, walk through the text once with a few rules, or do digit arithmetic?

### On paper

**Problem A: Count it.** Run `isAnagram` of section 2.1 on `s = "abca"`, `t = "cabb"`. Write the counters after `s` and after each character of `t`, and say where it stops.
*Where:* paper. *How:* the table of section 2.1.

**Problem B: Centres.** (a) How many centres does a string of length 7 have? (b) For `"abba"`, write how many palindromes each centre finds (the counting version of section 3) and name the longest palindrome and its centre.
*Where:* paper. *How:* list `c = 0..6` with the starting `(l, r)` for each.

**Problem C: Parse it.** Give the result of `myAtoi` for `"  -0012a34"`, `"+ 7"`, `"4193 with words"`, `"0000000000012345678"`, `"2147483650"` and `"1000000000000"`. For the last two, say at which digit the overflow test fires.
*Where:* paper. *How:* the three steps of section 4.1; the bound is 214748364 unless the digit is 8 or 9.

**Problem D: Multiply.** Trace `multiplyStrings("99", "99")`: write the array `pos` after each of the four digit pairs. What is the largest value any cell holds?
*Where:* paper. *How:* the table of section 5.2.

**Problem E: Overlap.** Starting from `abcdefgh`, give the result of (a) `memmove(buf + 1, buf, 5)` and what a front-to-back copy would produce instead, (b) `memmove(buf, buf + 3, 5)`.
*Where:* paper. *How:* decide the direction from where `dst` starts, then copy.

### In code

**Problem 1: Valid Anagram** (LeetCode 242, easy)
*Where:* leetcode.com/problems/valid-anagram. *Goal:* are `s` and `t` anagrams?
*How:* section 2.1. Then answer the follow-up: the inputs contain Unicode characters. (Decode to code points and count in a hash map.)

**Problem 2: Group Anagrams** (LeetCode 49, medium)
*Where:* leetcode.com/problems/group-anagrams. *Goal:* group the words that are anagrams of each other.
*How:* section 2.3. Write the sorted key first, then the count key, and state both complexities.

**Problem 3: Longest Palindromic Substring** (LeetCode 5, medium)
*Where:* leetcode.com/problems/longest-palindromic-substring. *Goal:* return the longest substring that is a palindrome.
*How:* section 3. Test `"a"`, `"bb"`, `"abba"` and `"abcd"` before submitting.

**Problem 4: String to Integer (atoi)** (LeetCode 8, medium)
*Where:* leetcode.com/problems/string-to-integer-atoi. *Goal:* implement `atoi` with the rules of section 4.1.
*How:* write the three steps, then the overflow test. Run the table of cases from section 4.1 against your code.

**Problem 5: Multiply Strings** (LeetCode 43, medium)
*Where:* leetcode.com/problems/multiply-strings. *Goal:* multiply two non-negative integers given as strings, without converting them to numbers.
*How:* section 5.2. First write `addStrings` (LeetCode 415) from section 5.1; it uses the same carry idea.

**Problem 6: Compare Version Numbers** (LeetCode 165, medium)
*Where:* leetcode.com/problems/compare-version-numbers. *Goal:* return -1, 0 or 1.
*How:* section 4.3. Solve it twice: once by splitting into vectors of integers, once by reading revisions on the fly.

**More practice, once these feel easy:** First Unique Character in a String (387), Palindromic Substrings (647), Reverse Words in a String (151, with the O(1)-space follow-up), Add Strings (415), Encode and Decode Strings (271, premium on LeetCode, free on neetcode.io), Reverse String (344), Isomorphic Strings (205), Ransom Note (383), Longest Common Prefix (14), Add Binary (67), Roman to Integer (13), Valid Number (65, hard to get right: write the cases first). Also write `strlen`, `strcpy` that reports truncation, and `memmove` in your own editor and test them against the library on random buffers, as the companion does. NeetCode's "Arrays & Hashing" and "Two Pointers" lists contain 242, 49, 125 and 271 with video walkthroughs.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };`; the logic is the same.

### Problem A

After counting `s`: `a: 2, b: 1, c: 1`. Then `c` gives `c: 0`, `a` gives `a: 1`, `b` gives `b: 0`, and the second `b` gives `b: -1`, so it stops at the **fourth** character of `t` and returns false. The lengths are equal, but `t` has two `b`s and `s` only one.

### Problem B

(a) 2 × 7 - 1 = **13** centres. (b) `"abba"` has 7 centres; the number of palindromes found at each is `1, 0, 1, 2, 1, 0, 1`, total **6** (`a`, `b`, `b`, `a`, `bb`, `abba`). The longest is `abba`, found from centre 3, the gap between the two `b`s (`l = 1`, `r = 2`, growing to `l = -1`, `r = 4`; length `4 - (-1) - 1 = 4`).

### Problem C

| Input | Result |
|---|---|
| `"  -0012a34"` | -12 (stops at `a`) |
| `"+ 7"` | 0 (a space after the sign) |
| `"4193 with words"` | 4193 |
| `"0000000000012345678"` | 12345678 (leading zeros keep `result` at 0) |
| `"2147483650"` | `INT_MAX`: `result` is 214748365 before the last digit `0`, and 214748365 > 214748364, so it fires at the **10th (last) digit** |
| `"1000000000000"` | `INT_MAX`: `result` is 1000000000 before the 11th digit, and 1000000000 > 214748364, so it fires at the **11th digit** |

### Problem D

| `(i, j)` | Value | `pos` afterwards |
|---|---|---|
| (1, 1) | 9 × 9 + 0 = 81 | `[0, 0, 8, 1]` |
| (1, 0) | 9 × 9 + 8 = 89 | `[0, 8, 9, 1]` |
| (0, 1) | 9 × 9 + 9 = 90 | `[0, 17, 0, 1]` |
| (0, 0) | 9 × 9 + 17 = 98 | `[9, 8, 0, 1]` |

The result is `9801`. The largest value any cell holds is **17**, in `pos[1]`, before the last pair reduces it.

### Problem E

(a) `dst = buf + 1` starts inside `src = buf .. buf + 4`, so copy **back to front**: the result is `aabcdegh`. Front to back would produce `aaaaaagh`, because each copied `a` is read again as a source.
(b) `dst = buf` is below `src = buf + 3`, so front to back is safe: the result is `defghfgh`.

### Problem 1: Valid Anagram

The function `isAnagram` of section 2.1 is the answer: equal lengths, count up for `s`, count down for `t`, fail on a negative counter. `"anagram"` and `"nagaram"` give true, `"rat"` and `"car"` give false. O(n) time, O(1) space. For the Unicode follow-up, replace the array with `unordered_map<char32_t, int>` over decoded code points.

### Problem 2: Group Anagrams

The function `groupAnagrams` of section 2.3 (sorted key) is the answer, and `groupAnagramsByCount` is the count-key version. For `["eat", "tea", "tan", "ate", "nat", "bat"]` the groups are `[eat, tea, ate]`, `[tan, nat]`, `[bat]` in any order. O(n × L log L) with the sorted key, O(n × (L + 26)) with the count key.

### Problem 3: Longest Palindromic Substring

The function `longestPalindrome` of section 3 is the answer. `"babad"` gives `bab`, `"cbbd"` gives `bb`, `"a"` gives `a`. O(n²) time, O(1) space.

### Problem 4: String to Integer (atoi)

The function `myAtoi` of section 4.1 is the answer. The table of cases there lists the inputs to test. O(n) time, O(1) space.

### Problem 5: Multiply Strings

The function `multiplyStrings` of section 5.2 is the answer, with `addStrings` of section 5.1 as the warm-up. `"123"` times `"456"` gives `"56088"`; `"0"` times anything gives `"0"`. O(la × lb) time, O(la + lb) space.

### Problem 6: Compare Version Numbers

The function `compareVersion` of section 4.3 is the answer (reading revisions on the fly, O(1) space). The splitting version uses `split` of section 4.2, converts each piece to a number and pads the shorter list with zeros.

---

## Cheat sheet

- **Bytes:** `size()` counts bytes. `char` may be signed: cast to `unsigned char` before indexing or calling `<cctype>`. UTF-8 characters are the bytes that are not `10xxxxxx`.
- **Costs:** `size`, `[]`, `push_back` O(1); `+`, `substr`, copy, `==` O(length); `insert(0, ...)` O(n); a string hash key costs O(length) per lookup.
- **Anagram:** equal length, counter array, fail on a negative counter. **Grouping:** canonical key (sorted letters, or counts with a `#` separator) in a hash map.
- **Palindromes:** 2n - 1 centres; `l = c / 2`, `r = l + c % 2`; length `r - l - 1`, start `l + 1`; O(n²) time, O(1) space. Manacher is O(n).
- **atoi:** spaces, sign, digits; clamp with `result > (INT_MAX - d) / 10` before multiplying. Widen to `long long` before negating `INT_MIN`.
- **Split:** keep empty fields; `join` is the inverse. **Versions:** compare revisions as numbers, missing = 0. **Reverse words:** reverse all, then each word.
- **Encode lists:** length, separator, bytes. Never rely on a delimiter alone.
- **Digit strings:** add from the right with a carry (loop while a carry remains); multiply with `pos[i + j + 1]` for the units digit and `pos[i + j]` for the carry.
- **Building:** `+=` and `reserve`; never `s = s + c`; build backwards and reverse once instead of inserting at the front; use views instead of `substr`.
- **C strings:** `strlen` is O(n); `strncpy` may not terminate; use `snprintf`. **memmove:** `dst` below `src` (or no overlap) front to back, else back to front.

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `07_string_basics_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** area 02, Hashing. Say "next" when you are ready.
