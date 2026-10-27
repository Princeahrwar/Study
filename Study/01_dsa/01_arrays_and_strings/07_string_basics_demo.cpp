// Tutorial 07: string basics. Every function the tutorial shows is here, followed by brute-force
// versions and randomized checks.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 07_string_basics_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- section 2: counting
// LeetCode 242: do s and t use exactly the same characters, the same number of times?
bool isAnagram(const std::string& s, const std::string& t) {
    if (s.size() != t.size()) return false;                   // different lengths can never match
    int count[256] = {0};                                     // one counter per possible byte value
    for (unsigned char c : s) ++count[c];
    for (unsigned char c : t)
        if (--count[c] < 0) return false;                     // t has more of this character than s does
    return true;
}

// LeetCode 387: the index of the first character that occurs exactly once, or -1.
int firstUniqChar(const std::string& s) {
    int count[256] = {0};
    for (unsigned char c : s) ++count[c];                     // pass 1: count everything
    for (size_t i = 0; i < s.size(); ++i)
        if (count[(unsigned char)s[i]] == 1) return (int)i;   // pass 2: the first character that is unique
    return -1;
}

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

// ---------------------------------------------------------------- section 3: palindromes
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

// LeetCode 647: how many substrings are palindromes (equal substrings at different places count separately)?
int countPalindromicSubstrings(const std::string& s) {
    int n = (int)s.size(), count = 0;
    for (int center = 0; center < 2 * n - 1; ++center) {
        int l = center / 2, r = l + center % 2;
        while (l >= 0 && r < n && s[l] == s[r]) { ++count; --l; ++r; }   // every successful growth step is one more palindrome
    }
    return count;
}

// ---------------------------------------------------------------- section 4: parsing
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

// ---------------------------------------------------------------- section 5: big numbers as strings
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

// ---------------------------------------------------------------- section 1 and 6: bytes, views, building
// How many characters (code points) does a UTF-8 string hold? s.size() counts bytes.
// Every byte is either a start byte or a continuation byte of the form 10xxxxxx; count the start bytes.
int utf8Length(const std::string& s) {
    int count = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++count;                      // not a continuation byte: a new character starts here
    return count;
}

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

// ---------------------------------------------------------------- section 7: C strings
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

// ================================================================ tests
// Allocation counter: replaces the global operator new so the tests can count heap allocations.
static long g_allocs = 0;
static bool g_counting = false;
void* operator new(std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static std::mt19937 rng(7);
static int rnd(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
static std::string randomString(const std::string& alphabet, int maxLen) {
    int len = rnd(0, maxLen);
    std::string s;
    for (int i = 0; i < len; ++i) s.push_back(alphabet[rnd(0, (int)alphabet.size() - 1)]);
    return s;
}

// ---- brute forces
static bool bruteAnagram(std::string a, std::string b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}
static int bruteFirstUniq(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        int c = 0;
        for (char x : s) c += (x == s[i]);
        if (c == 1) return (int)i;
    }
    return -1;
}
static bool isPal(const std::string& s, int l, int r) {          // s[l..r] inclusive
    while (l < r) if (s[l++] != s[r--]) return false;
    return true;
}
static std::string brutePalindrome(const std::string& s) {
    int n = (int)s.size(), bestStart = 0, bestLen = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j)
            if (isPal(s, i, j) && j - i + 1 > bestLen) { bestLen = j - i + 1; bestStart = i; }
    return s.substr(bestStart, bestLen);
}
static int brutePalCount(const std::string& s) {
    int n = (int)s.size(), c = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j) c += isPal(s, i, j);
    return c;
}
static int bruteAtoi(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    std::string digits;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') digits.push_back(s[i++]);
    size_t nz = 0;
    while (nz < digits.size() && digits[nz] == '0') ++nz;
    digits = digits.substr(nz);
    if (digits.size() > 12) return neg ? INT_MIN : INT_MAX;
    long long v = 0;
    for (char c : digits) v = v * 10 + (c - '0');
    if (neg) v = -v;
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return (int)v;
}
static std::vector<std::string> bruteSplit(const std::string& s, char d) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == d) out.emplace_back();
        else out.back().push_back(c);
    }
    return out;
}
static int bruteCompareVersion(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s) {
        std::vector<long long> v;
        for (const auto& part : bruteSplit(s, '.')) v.push_back(std::stoll(part));
        return v;
    };
    std::vector<long long> x = parse(a), y = parse(b);
    size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    if (x == y) return 0;
    return x < y ? -1 : 1;
}
static std::string bruteReverseWords(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> w;
    std::string t;
    while (in >> t) w.push_back(t);
    std::reverse(w.begin(), w.end());
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) out += (i ? " " : "") + w[i];
    return out;
}
static std::string toStr128(unsigned __int128 v) {
    if (v == 0) return "0";
    std::string s;
    while (v) { s.push_back(char('0' + (int)(v % 10))); v /= 10; }
    std::reverse(s.begin(), s.end());
    return s;
}
static unsigned __int128 randomBig(int maxDigits) {
    int d = rnd(1, maxDigits);
    unsigned __int128 v = 0;
    for (int i = 0; i < d; ++i) v = v * 10 + (unsigned)rnd(0, 9);
    return v;
}
static std::vector<std::vector<std::string>> canonical(std::vector<std::vector<std::string>> g) {
    for (auto& grp : g) std::sort(grp.begin(), grp.end());
    std::sort(g.begin(), g.end());
    return g;
}
static std::vector<std::vector<std::string>> bruteGroups(const std::vector<std::string>& words) {
    std::vector<std::vector<std::string>> g;
    std::vector<bool> used(words.size(), false);
    for (size_t i = 0; i < words.size(); ++i) {
        if (used[i]) continue;
        std::vector<std::string> grp;
        for (size_t j = i; j < words.size(); ++j)
            if (!used[j] && bruteAnagram(words[i], words[j])) { used[j] = true; grp.push_back(words[j]); }
        g.push_back(grp);
    }
    return g;
}
static std::string encodeUtf8(unsigned cp) {
    std::string s;
    if (cp < 0x80) s.push_back((char)cp);
    else if (cp < 0x800) { s.push_back((char)(0xC0 | (cp >> 6))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        s.push_back((char)(0xE0 | (cp >> 12)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        s.push_back((char)(0xF0 | (cp >> 18)));
        s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return s;
}

int main() {
    // ---- anagrams
    CHECK(isAnagram("anagram", "nagaram"));
    CHECK(!isAnagram("rat", "car"));
    CHECK(isAnagram("", ""));
    CHECK(!isAnagram("a", "ab"));
    CHECK(!isAnagram("aab", "abb"));
    for (int it = 0; it < 4000; ++it) {
        std::string alpha = (it % 2) ? "abc" : std::string("ab\xE9\xFF\x80", 5);   // second alphabet has bytes above 127
        std::string a = randomString(alpha, 7), b = randomString(alpha, 7);
        CHECK(isAnagram(a, b) == bruteAnagram(a, b));
        std::string shuffled = a;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        CHECK(isAnagram(a, shuffled));
    }
    CHECK(firstUniqChar("leetcode") == 0);
    CHECK(firstUniqChar("loveleetcode") == 2);
    CHECK(firstUniqChar("aabb") == -1);
    CHECK(firstUniqChar("") == -1);
    for (int it = 0; it < 3000; ++it) {
        std::string s = randomString("abcd\xE9", 9);
        CHECK(firstUniqChar(s) == bruteFirstUniq(s));
    }

    // ---- group anagrams
    {
        std::vector<std::string> w = {"eat", "tea", "tan", "ate", "nat", "bat"};
        auto expected = canonical({{"bat"}, {"nat", "tan"}, {"ate", "eat", "tea"}});
        CHECK(canonical(groupAnagrams(w)) == expected);
        CHECK(canonical(groupAnagramsByCount(w)) == expected);
        CHECK(canonical(groupAnagrams({""})) == canonical({{""}}));
        CHECK(groupAnagrams({}).empty());
        std::string x = "a" + std::string(11, 'b'), y = std::string(11, 'a') + "b";
        CHECK(countKey(x) != countKey(y));                         // the separator keeps 1,11 apart from 11,1
        CHECK(countKey("aab") == "2#1#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#0#");
        CHECK(canonical(groupAnagramsByCount({x, y})).size() == 2);
        for (int it = 0; it < 1500; ++it) {
            std::vector<std::string> words;
            int n = rnd(0, 8);
            for (int i = 0; i < n; ++i) words.push_back(randomString("abc", 4));
            auto expect = canonical(bruteGroups(words));
            CHECK(canonical(groupAnagrams(words)) == expect);
            CHECK(canonical(groupAnagramsByCount(words)) == expect);
        }
    }

    // ---- palindromes
    CHECK(longestPalindrome("babad") == "bab");
    CHECK(longestPalindrome("cbbd") == "bb");
    CHECK(longestPalindrome("a") == "a");
    CHECK(longestPalindrome("") == "");
    CHECK(longestPalindrome("forgeeksskeegfor") == "geeksskeeg");
    CHECK(countPalindromicSubstrings("abc") == 3);
    CHECK(countPalindromicSubstrings("aaa") == 6);
    CHECK(countPalindromicSubstrings("") == 0);
    for (int it = 0; it < 4000; ++it) {
        std::string s = randomString(it % 2 ? "ab" : "abc", 12);
        CHECK(longestPalindrome(s) == brutePalindrome(s));
        CHECK(countPalindromicSubstrings(s) == brutePalCount(s));
    }

    // ---- parsing
    const char* atoiCases[] = {"42", "   -042", "1337c0d3", "0-1", "words and 987", "-91283472332", "2147483647",
                               "2147483648", "-2147483648", "-2147483649", "+-12", "00000000000000000000000000012",
                               "", "+", "  +  413", "   ", "-", "9999999999999999999999", "  +0 123", "+7"};
    for (const char* c : atoiCases) CHECK(myAtoi(c) == bruteAtoi(c));
    CHECK(myAtoi("42") == 42);
    CHECK(myAtoi("   -042") == -42);
    CHECK(myAtoi("1337c0d3") == 1337);
    CHECK(myAtoi("0-1") == 0);
    CHECK(myAtoi("words and 987") == 0);
    CHECK(myAtoi("-91283472332") == INT_MIN);
    CHECK(myAtoi("2147483647") == INT_MAX);
    CHECK(myAtoi("2147483648") == INT_MAX);
    CHECK(myAtoi("-2147483648") == INT_MIN);
    CHECK(myAtoi("-2147483649") == INT_MIN);
    for (int it = 0; it < 6000; ++it) {
        std::string s = randomString(" +-0123456789a.", 14);
        CHECK(myAtoi(s) == bruteAtoi(s));
    }
    for (int it = 0; it < 4000; ++it) {                            // numbers close to the limits
        long long base = (it % 2) ? (long long)INT_MAX : (long long)INT_MIN;
        long long v = base + rnd(-30, 30);
        int spaces = rnd(0, 3);
        bool plus = rnd(0, 3) == 0;
        std::string s = std::string(spaces, ' ') + (plus ? "+" : "") + std::to_string(v);
        if (v >= 0 && s.find('+') == std::string::npos && rnd(0, 1)) s += "xyz";
        CHECK(myAtoi(s) == bruteAtoi(s));
    }
    {
        int edge[] = {0, 1, -1, 9, 10, -10, 123456789, INT_MAX, INT_MIN, INT_MIN + 1, 1000000000};
        for (int v : edge) CHECK(intToString(v) == std::to_string(v));
        for (int it = 0; it < 6000; ++it) {
            unsigned hi = (unsigned)rng(), lo = (unsigned)rng();
            int v = (int)(hi ^ (lo << 7));
            CHECK(intToString(v) == std::to_string(v));
            CHECK(myAtoi(intToString(v)) == v);                    // the two functions are inverses
        }
    }
    {
        CHECK((split("a,,b,", ',') == std::vector<std::string>{"a", "", "b", ""}));
        CHECK((split("", ',') == std::vector<std::string>{""}));
        CHECK((split(",", ',') == std::vector<std::string>{"", ""}));
        CHECK((split("abc", ',') == std::vector<std::string>{"abc"}));
        for (int it = 0; it < 4000; ++it) {
            std::string s = randomString("ab,.", 12);
            auto parts = split(s, ',');
            CHECK(parts == bruteSplit(s, ','));
            CHECK(join(parts, ",") == s);                          // joining the pieces gives the text back
            auto views = splitView(s, ',');
            bool same = views.size() == parts.size();
            for (size_t i = 0; same && i < views.size(); ++i) same = (views[i] == parts[i]);
            CHECK(same);
        }
        CHECK(join({}, ", ") == "");
        CHECK(join({"x"}, ", ") == "x");
        CHECK(join({"x", "y", "z"}, ", ") == "x, y, z");
        for (int it = 0; it < 1000; ++it) {
            std::vector<std::string> parts;
            int n = rnd(0, 6);
            for (int i = 0; i < n; ++i) parts.push_back(randomString("ab", 3));
            std::string sep = randomString("-+", 2), expect;
            for (size_t i = 0; i < parts.size(); ++i) { if (i) expect += sep; expect += parts[i]; }
            CHECK(join(parts, sep) == expect);
        }
    }
    CHECK(compareVersion("1.01", "1.001") == 0);
    CHECK(compareVersion("1.0", "1.0.0") == 0);
    CHECK(compareVersion("0.1", "1.1") == -1);
    CHECK(compareVersion("1.0.1", "1") == 1);
    CHECK(compareVersion("7.5.2.4", "7.5.3") == -1);
    CHECK(compareVersion("1.2147483648", "1.2147483647") == 1);
    for (int it = 0; it < 6000; ++it) {
        auto make = [&]() {
            std::string v;
            int parts = rnd(1, 4);
            for (int i = 0; i < parts; ++i) {
                if (i) v += '.';
                int zeros = rnd(0, 2), number = rnd(0, 12);
                v += std::string(zeros, '0') + std::to_string(number);
            }
            return v;
        };
        std::string a = make(), b = make();
        CHECK(compareVersion(a, b) == bruteCompareVersion(a, b));
        CHECK(compareVersion(a, b) == -compareVersion(b, a));
    }
    CHECK(reverseWords("the sky is blue") == "blue is sky the");
    CHECK(reverseWords("  hello world  ") == "world hello");
    CHECK(reverseWords("a good   example") == "example good a");
    CHECK(reverseWords("") == "");
    CHECK(reverseWords("   ") == "");
    CHECK(reverseWords("word") == "word");
    for (int it = 0; it < 6000; ++it) {
        std::string s = randomString("ab  ", 14);
        CHECK(reverseWords(s) == bruteReverseWords(s));
    }
    {
        CHECK(decodeStrings(encodeStrings({})).empty());
        CHECK((decodeStrings(encodeStrings({""})) == std::vector<std::string>{""}));
        CHECK(encodeStrings({"ab", "", "c#d"}) == "2#ab0#3#c#d");
        for (int it = 0; it < 4000; ++it) {
            std::vector<std::string> strs;
            int n = rnd(0, 4);
            for (int i = 0; i < n; ++i) strs.push_back(randomString(std::string("#05 a\0", 6), 5));
            CHECK(decodeStrings(encodeStrings(strs)) == strs);
        }
    }

    // ---- big numbers
    CHECK(addStrings("11", "123") == "134");
    CHECK(addStrings("456", "77") == "533");
    CHECK(addStrings("0", "0") == "0");
    CHECK(addStrings("999", "1") == "1000");
    CHECK(addStrings("", "") == "0");
    CHECK(multiplyStrings("2", "3") == "6");
    CHECK(multiplyStrings("123", "456") == "56088");
    CHECK(multiplyStrings("0", "123") == "0");
    CHECK(multiplyStrings("999", "999") == "998001");
    CHECK(multiplyStrings("9133", "0") == "0");
    CHECK(multiplyStrings("99999999999", "99999999999") == "9999999999800000000001");
    for (int it = 0; it < 5000; ++it) {
        unsigned __int128 x = randomBig(30), y = randomBig(30);
        CHECK(addStrings(toStr128(x), toStr128(y)) == toStr128(x + y));
    }
    for (int it = 0; it < 5000; ++it) {
        unsigned __int128 x = randomBig(18), y = randomBig(18);
        CHECK(multiplyStrings(toStr128(x), toStr128(y)) == toStr128(x * y));
    }
    for (int a = 0; a <= 30; ++a)
        for (int b = 0; b <= 30; ++b) {
            std::string acc = "0";                                 // repeated addition is an independent way to multiply
            for (int k = 0; k < b; ++k) acc = addStrings(acc, std::to_string(a));
            CHECK(multiplyStrings(std::to_string(a), std::to_string(b)) == acc);
        }

    // ---- bytes, views, building
    CHECK(utf8Length("hello") == 5);
    CHECK(std::string("h\xC3\xA9llo").size() == 6 && utf8Length("h\xC3\xA9llo") == 5);
    CHECK(std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E").size() == 9 && utf8Length("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 3);
    CHECK(std::string("\xF0\x9F\x98\x80").size() == 4 && utf8Length("\xF0\x9F\x98\x80") == 1);
    CHECK(utf8Length("") == 0);
    {
        unsigned cps[] = {0x41, 0x7A, 0xE9, 0x65E5, 0x1F600, 0x20};
        for (int it = 0; it < 3000; ++it) {
            std::string s;
            int n = rnd(0, 10);
            for (int i = 0; i < n; ++i) s += encodeUtf8(cps[rnd(0, 5)]);
            CHECK(utf8Length(s) == n);
        }
    }
    {
        // Allocation counts for building a 1000-character string three ways.
        const int N = 1000;
        g_allocs = 0; g_counting = true;
        { std::string s; for (int i = 0; i < N; ++i) s += 'x'; CHECK(s.size() == (size_t)N); }
        long appendAllocs = g_allocs;
        g_allocs = 0;
        { std::string s; s.reserve(N); for (int i = 0; i < N; ++i) s += 'x'; CHECK(s.size() == (size_t)N); }
        long reserveAllocs = g_allocs;
        g_allocs = 0;
        { std::string s; for (int i = 0; i < N; ++i) s = s + 'x'; CHECK(s.size() == (size_t)N); }
        long plusAllocs = g_allocs;
        g_counting = false;
        CHECK(appendAllocs >= 2 && appendAllocs <= 30);            // geometric growth: a handful of allocations
        CHECK(reserveAllocs == 1);                                 // one allocation, sized up front
        CHECK(plusAllocs >= 900);                                  // roughly one new buffer per character
        std::printf("allocations for 1000 appends: += %ld, reserve + += %ld, s = s + c %ld\n",
                    appendAllocs, reserveAllocs, plusAllocs);
    }

    // ---- C strings
    {
        char buf[] = "abcdef";
        myMemmove(buf + 2, buf, 4);
        CHECK(std::string(buf) == "ababcd");
        char naive[] = "abcdef";
        for (size_t i = 0; i < 4; ++i) (naive + 2)[i] = naive[i];  // a front-to-back copy of overlapping data
        CHECK(std::string(naive) == "ababab");                     // ... smears the first two bytes
        char back[] = "abcdef";
        myMemmove(back, back + 2, 4);
        CHECK(std::string(back) == "cdefef");
        for (int it = 0; it < 6000; ++it) {
            unsigned char a[40], b[40];
            for (int i = 0; i < 40; ++i) a[i] = b[i] = (unsigned char)rnd(0, 255);
            int s = rnd(0, 20), d = rnd(0, 20), n = rnd(0, 20);
            void* r1 = myMemmove(a + d, a + s, (size_t)n);
            void* r2 = std::memmove(b + d, b + s, (size_t)n);
            CHECK(r1 == a + d && r2 == b + d);
            CHECK(std::memcmp(a, b, 40) == 0);
        }
        for (int it = 0; it < 4000; ++it) {
            std::string src = randomString("abcxyz", 10);
            size_t cap = (size_t)rnd(0, 8);
            char d1[12], d2[12];
            std::memset(d1, 'X', sizeof d1);
            std::memset(d2, 'X', sizeof d2);
            size_t want = safeCopy(d1, cap, src.c_str());
            int want2 = std::snprintf(d2, cap, "%s", src.c_str());
            CHECK(want == (size_t)want2);
            CHECK(std::memcmp(d1, d2, sizeof d1) == 0);
            CHECK(cap == 0 || std::strlen(d1) < cap);              // always terminated inside the buffer
        }
    }

    std::printf("information only: sizeof(std::string) = %zu, sizeof(std::string_view) = %zu, "
                "capacity of an empty string = %zu, char is %s\n",
                sizeof(std::string), sizeof(std::string_view), std::string().capacity(),
                (char)-1 < 0 ? "signed" : "unsigned");
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
