# DSA 14.02: Randomised and Approximate Algorithms

*Part of the [Theory and advanced techniques plan](00_plan.md) · Time: about 7 hours (3.5 reading, 3.5 problems) · You need: [P, NP and what to do with a hard problem](01_p_np_and_hard_problems.md) (vertex cover, TSP, why exact search stops), quicksort and selection from [Binary search and sorting](../03_binary_search_and_sorting/00_plan.md), and basic probability (expectation, independent events)*

The last tutorial ended with a list of ways to answer an NP-hard problem. Two of them need coins: an **approximation** gives up some quality and keeps a proof of how much, and a **heuristic** such as simulated annealing gives up the proof and keeps the speed. This tutorial also covers the other, older use of randomness: making an algorithm *fast with high probability* when no deterministic algorithm is as simple or as quick (quicksort, selection, checking a matrix product, testing primality, finding a minimum cut). The questions are always the same: what exactly is random, what is guaranteed, and what does it cost to make the failure probability as small as you like.

By the end you can:

- tell a **Las Vegas** algorithm (always correct, random running time) from a **Monte Carlo** algorithm (fast, correct with high probability), and say on which side the error lies;
- compute an expected running time, and lower the failure probability of a Monte Carlo algorithm by repeating it (measured on four examples);
- write randomised quicksort and quickselect, and explain why a random pivot beats any input (measured);
- check a matrix product with Freivalds' method, test primality with Miller-Rabin, and find a minimum cut with Karger's contraction, and say what each guarantees;
- give a 2-approximation for vertex cover and the greedy algorithm for set cover, and show the instance where greedy is worst (measured);
- write a local search and simulated annealing for maximum cut, and compare both with a random assignment and with the optimum;
- answer "can you do better than exponential?" for a hard problem in a few sentences that say what is guaranteed.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 1,744,300 checks passed under AddressSanitizer and UBSan. Deterministic functions are compared with plain oracles (a sort, a sieve and trial division, all subsets for the optimum of vertex cover, set cover, minimum cut and maximum cut). For the random ones the tests check what must hold on every run (the answer is a valid cover, the cut is a real cut, the result is never better than the optimum) and, separately, how often the lucky outcome happens, with bounds wide enough to hold for the fixed seeds used. I tried 24 deliberate bugs. All were caught, but two escaped the first tests and I added tests for them: Karger's contraction that counts a merge that did not happen (the answer was still a valid cut, only worse), and an annealing acceptance rule with the wrong sign (the best cut seen was still valid, and on tiny graphs a random walk finds the optimum anyway). The tests now include the single-run success rate of Karger's algorithm and annealing on 30-vertex bipartite graphs. Two other bugs were caught indirectly: a swapped branch in quickselect crashed the program (division by zero in the random pivot), and a local search that flips on ties never terminated (killed by a time limit). I did not mutate the rejection loop in `Rng::below`: its bias is about `n / 2^64`, which no test can see.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. All random choices come from a seeded generator, so every number is repeatable. Graphs and matrices are random; treat the numbers as the shape of the effect, not as a prediction for your data. Statements about theory (proven bounds, hardness of approximation, behaviour of real libraries) are from memory and marked.

---

## 1. Two kinds of randomness

A **Las Vegas** algorithm always returns the correct answer; only its running time depends on the coin flips. Randomised quicksort is the standard example. You reason about its **expected** time.

A **Monte Carlo** algorithm has a fixed running time and returns the correct answer with some probability. You reason about its **error probability**. It is **one-sided** when one of the two answers is always right. Primality testing is the classic case: "composite" is always correct (and comes with a witness), "prime" may be wrong. It is **two-sided** when either answer can be wrong (estimating a fraction by sampling).

Three facts to carry into every example:

1. **Amplification.** Repeat a one-sided Monte Carlo algorithm `k` times and answer "wrong side" only if every run does. If one run fails with probability at most `p`, `k` runs fail with probability at most `p^k`. Ten runs of a coin-flip test leave 1 in 1,024; thirty leave about 1 in a billion. With the error probability below the chance of a cosmic ray flipping a bit in your computer, the algorithm is as good as deterministic.
2. **From Las Vegas to Monte Carlo.** If the expected time is `E`, then by Markov's inequality the probability of running longer than `2E` is at most 1/2. Stop at `2E` and answer "give up": you now have a Monte Carlo algorithm. Going the other way needs a way to check an answer.
3. **Randomness defends against inputs, not against unlucky coins.** A deterministic rule ("pivot is the first element") can be defeated by an adversary who knows it. A random rule cannot be defeated by any input, because the coins are chosen after the input.

All the examples use this small generator, seeded so that every run is repeatable. (`rand() % n` is biased and shares one global state; avoid it.)

```cpp
// A small deterministic generator (splitmix64), so every run of every example is repeatable.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) {                       // uniform in [0, n); rejecting the first 2^64 mod n values removes the bias
        uint64_t skip = (0 - n) % n, x;
        do x = next(); while (x < skip);
        return x % n;
    }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }   // uniform in [0, 1)
};
```

## 2. Randomised quicksort and quickselect

Quicksort picks a pivot, splits the array into smaller and larger elements, and recurses. With the first element as pivot, a sorted array makes every split `0 : n-1`, so the work is `n(n-1)/2` comparisons. Any fixed rule has such an input. A **random** pivot has none: the expected number of comparisons is the same for every input. For distinct keys it is exactly `2(n+1)H(n) - 4n`, where `H(n) = 1 + 1/2 + ... + 1/n`, about `1.39 n log2 n`.

Why: element `i` and element `j` (in sorted order) are compared exactly when one of them is the first pivot chosen among the `j - i + 1` elements between them and including them, which has probability `2 / (j - i + 1)`. Add over all pairs.

```cpp
// Randomised quicksort (Las Vegas: always sorted, running time is random). `cmp` counts element comparisons.
enum class Pivot { First, Random };
inline void quicksort(vector<int>& a, int lo, int hi, Pivot mode, Rng& rng, long long& cmp) {
    while (lo < hi) {
        int p = mode == Pivot::First ? lo : lo + (int)rng.below(hi - lo + 1);
        std::swap(a[p], a[hi]);
        int pv = a[hi], i = lo;
        for (int j = lo; j < hi; ++j) { ++cmp; if (a[j] < pv) std::swap(a[i++], a[j]); }
        std::swap(a[i], a[hi]);
        if (i - lo < hi - i) { quicksort(a, lo, i - 1, mode, rng, cmp); lo = i + 1; }   // recurse on the smaller side:
        else { quicksort(a, i + 1, hi, mode, rng, cmp); hi = i - 1; }                    // the stack stays O(log n)
    }
}

// Randomised selection: the k-th smallest (k from 0), expected O(n) comparisons.
inline int quickselect(vector<int> a, int k, Rng& rng, long long& cmp) {
    int lo = 0, hi = (int)a.size() - 1;
    for (;;) {
        int p = lo + (int)rng.below(hi - lo + 1);
        std::swap(a[p], a[hi]);
        int pv = a[hi], i = lo;
        for (int j = lo; j < hi; ++j) { ++cmp; if (a[j] < pv) std::swap(a[i++], a[j]); }
        std::swap(a[i], a[hi]);
        if (i == k) return a[i];
        if (k < i) hi = i - 1; else lo = i + 1;
    }
}
```

(The loop recurses into the smaller side, so the stack stays `O(log n)` even in a bad run.)

```
T1 n=3000 already sorted: first element as pivot 4498500 comparisons (n(n-1)/2 = 4498500); random pivot 38026
T1 n=3000, 300 random orders, random pivot: average 39499 comparisons (formula 2(n+1)H(n) - 4n = 39520), fewest 36003, most 49061
```

An already sorted array costs the first-element rule `n(n-1)/2 = 4,498,500` comparisons and the random rule 38,026. Over 300 random orders the average (39,499) is within 0.1 percent of the formula (39,520). The spread is worth noticing: the best run used 36,003 and the worst 49,061. A bad run exists, it is just rare, and it does not depend on the input.

| Sorted array of 20,000, time (ms) | run 1 / run 2 |
|---|---|
| first element as pivot (about `2e8` comparisons) | 63.0 / 62.6 |
| random pivot | 0.58 / 0.59 |

**Quickselect** finds the `k`-th smallest by partitioning and recursing into one side only. The expected work is `O(n)`: the median needs about `3.4 n` comparisons (the constant is from memory).

```
T2 n=100000, 200 random orders, comparisons divided by n: median 3.46 (worst run 7.31); minimum 1.97; 10th percentile 2.66
```

The measured average is `3.46 n` for the median and less for the extremes. The worst of 200 runs used `7.31 n`; the cost has a long tail, which is why production versions use a fallback (introselect, or median of medians for a guaranteed `O(n)`, from memory).

## 3. Checking a matrix product (Freivalds)

Given `n x n` matrices `A`, `B`, `C`, is `A * B = C`? Multiplying takes `O(n^3)` with the schoolbook method. Pick a random vector `x` of zeros and ones and compare `A(Bx)` with `Cx`. Each side is two matrix-vector products, `O(n^2)`.

- If `AB = C` the two sides are always equal: never an error.
- If `AB != C`, some entry of `D = AB - C` is nonzero, say row `i`. The `i`-th entry of `Dx` is a sum in which the nonzero entry is multiplied by an independent coin; flipping that one coin changes the sum, so at most one of the two values of that coin gives zero. The error probability per round is at most 1/2.

```cpp
// Monte Carlo: is A * B == C ? Multiplying costs O(n^3) (or about n^2.8 with fast algorithms); checking costs O(n^2) per round.
// If A*B == C the answer is always "equal". If not, one round says "equal" with probability at most 1/2.
using Mat = vector<vector<long long>>;
inline vector<long long> mat_vec(const Mat& M, const vector<long long>& x) {
    vector<long long> y(M.size(), 0);
    for (size_t i = 0; i < M.size(); ++i) for (size_t j = 0; j < x.size(); ++j) y[i] += M[i][j] * x[j];
    return y;
}
inline bool freivalds(const Mat& A, const Mat& B, const Mat& C, int rounds, Rng& rng) {
    size_t n = A.size();
    for (int r = 0; r < rounds; ++r) {
        vector<long long> x(n);
        for (auto& v : x) v = (long long)rng.below(2);              // a random 0/1 vector
        if (mat_vec(A, mat_vec(B, x)) != mat_vec(C, x)) return false;   // A(Bx) against Cx: two products with a vector
    }
    return true;
}
inline Mat mat_mul(const Mat& A, const Mat& B) {
    size_t n = A.size();
    Mat C(n, vector<long long>(n, 0));
    for (size_t i = 0; i < n; ++i) for (size_t k = 0; k < n; ++k) for (size_t j = 0; j < n; ++j) C[i][j] += A[i][k] * B[k][j];
    return C;
}
```

One wrong entry is the hardest case, since only one coin matters, so the bound is tight:

```
T3 40x40 matrices, C = A*B: 10 rounds say equal. One entry of C changed by 1; 20000 trials each; wrongly accepted: 1 round 0.4996 (bound 0.5), 5 rounds 0.03250 (bound 0.03125), 10 rounds 0.00085 (bound 0.00098)
```

1, 5 and 10 rounds were fooled 0.4996, 0.0325 and 0.00085 of the time, against the bounds 0.5, 0.0313 and 0.00098. Time on 400 x 400 matrices:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| multiply `X * Y` | 25.9 / 32.5 |
| Freivalds, 10 rounds | 2.23 / 2.84 |

The check is about 10 times faster at `n = 400` and the gap widens with `n`, since the check is `O(n^2)` per round. This is the first instance of a recurring idea: **verifying is cheaper than computing, so randomise the verification.** The same idea checks polynomial identities (from memory).

## 4. Primality testing (Miller-Rabin)

**Fermat's test:** if `n` is prime then `a^(n-1) = 1 (mod n)` for every `a` not divisible by `n`. So if some `a` gives something else, `n` is certainly composite. The converse fails: a **Carmichael number** such as 561 = 3 * 11 * 17 passes for *every* base coprime to it.

**Miller-Rabin** strengthens it. Write `n - 1 = d * 2^r` with `d` odd. If `n` is prime, then in the sequence `a^d, a^(2d), ..., a^(2^r d)` (mod `n`), either the first term is 1 or some term before the last is `-1`. Why: a prime has only two square roots of 1, namely 1 and -1, so a sequence of squarings that reaches 1 must pass through -1 (or start at 1). If the sequence reaches 1 from something other than -1, that value is a nontrivial square root of 1 and `n` is composite. The base `a` is then a **witness**. For any odd composite `n`, at most a quarter of the bases `a` in `[2, n-2]` are **liars** (they pass); this bound is a theorem I am quoting from memory.

```cpp
// Monte Carlo primality. Fermat: if n is prime then a^(n-1) = 1 (mod n). Carmichael numbers pass for every coprime a.
// Miller-Rabin: n - 1 = d * 2^r with d odd. If n is prime, then a^d = 1 or a^(d*2^i) = -1 for some i < r.
// For an odd composite n, at most a quarter of the bases a in [2, n-2] pass.
inline uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) { return (unsigned __int128)a * b % m; }
inline uint64_t powmod(uint64_t a, uint64_t e, uint64_t m) {
    uint64_t r = 1; a %= m;
    while (e) { if (e & 1) r = mulmod(r, a, m); a = mulmod(a, a, m); e >>= 1; }
    return r;
}
inline bool fermat_passes(uint64_t n, uint64_t a) { return powmod(a, n - 1, n) == 1; }       // n odd, n >= 5, 2 <= a <= n-2
inline bool miller_rabin_passes(uint64_t n, uint64_t a) {                                    // n odd, n >= 5, 2 <= a <= n-2
    uint64_t d = n - 1; int r = 0;
    while ((d & 1) == 0) { d >>= 1; ++r; }
    uint64_t x = powmod(a, d, n);
    if (x == 1 || x == n - 1) return true;
    for (int i = 1; i < r; ++i) { x = mulmod(x, x, n); if (x == n - 1) return true; }
    return false;
}
// Deterministic for every 64-bit n: the first twelve primes as bases are enough (the bound is known to be above 3 * 10^24, from memory).
inline bool is_prime(uint64_t n) {
    static const uint64_t bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    if (n < 2) return false;
    for (uint64_t p : bases) { if (n == p) return true; if (n % p == 0) return false; }
    for (uint64_t a : bases) if (!miller_rabin_passes(n, a)) return false;
    return true;
}
// Randomised: `rounds` random bases; a composite survives with probability at most 4^-rounds.
inline bool is_prime_random(uint64_t n, int rounds, Rng& rng) {
    if (n < 2) return false;
    for (uint64_t p : {2ULL, 3ULL, 5ULL, 7ULL}) { if (n == p) return true; if (n % p == 0) return false; }
    if (n < 121) return true;                                     // no prime factor up to 7 and below 11*11 means prime
    for (int i = 0; i < rounds; ++i) if (!miller_rabin_passes(n, 2 + rng.below(n - 3))) return false;
    return true;
}
```

Count the bases that pass, for the Carmichael numbers below 9,000:

```
T4 composite   coprime bases passing Fermat   bases passing Miller-Rabin   (bases tried: 2 to n-2)
T4 561   318   8   (558 bases)
T4 1105   766   28   (1102 bases)
T4 1729   1294   160   (1726 bases)
T4 2465   1790   68   (2462 bases)
T4 2821   2158   268   (2818 bases)
T4 6601   5278   328   (6598 bases)
T4 8911   7126   1780   (8908 bases)
```

Fermat is fooled by *every* coprime base for each of these (the second column is large, and for 561 it is 318 of 558). Miller-Rabin is fooled by 8 of 558 bases for 561. The theorem's one quarter is a worst case:

```
T4 every odd composite from 9 to 10000 (3771 numbers): fraction of bases fooled, average 0.003085, worst 0.2373 (at 1891); the proven bound is 0.25
```

Across all odd composites up to 10,000 the average fraction of liars is 0.003 and the worst is 0.237 (at 1891). So one random base fools a typical composite with probability about 1 in 300, and a handful of rounds is overwhelming, but the proven guarantee per round is 1/4, and you quote that when it must hold for any input.

**Deterministic for 64-bit numbers.** Trying the first twelve primes as bases is correct for every `n < 3.3 * 10^24` (from memory), so `is_prime` above is exact for `uint64_t`. A single base is not enough, and neither are four:

```
T4 3215031751 = 151 * 751 * 28351, Miller-Rabin with base 2 passes, 3 passes, 5 passes, 7 passes, 11 fails, and the twelve-base is_prime says composite
T4 is_prime: 2^61 - 1 prime; 2^64 - 59 prime; 2147483647 * 2147483629 composite
```

3,215,031,751 = 151 * 751 * 28351 passes bases 2, 3, 5 and 7 and is caught by 11. Running time on 100,000 random 64-bit numbers:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| `is_prime` on 100,000 random 64-bit numbers (12 bases each when the number is prime) | 43.2 / 42.2 |

```
T4 of 100000 random odd 64-bit numbers, 4767 are prime (prime density near 2^64 is about 2/ln(2^64) = 0.0451 among odd numbers, so about 4508 expected)
```

The count of primes (4,767) is within the usual spread of the 4,508 expected from the prime number theorem. Each round costs `O(log n)` multiplications modulo `n`; with 64-bit values the multiplication is done in 128 bits to avoid overflow. Primality testing also underlies RSA key generation (from memory).

## 5. Minimum cut by random contraction (Karger)

A **cut** splits the vertices into two non-empty sides; its size is the number of edges between them. The minimum cut can be found by flows in polynomial time, but Karger's algorithm is shorter and shows a different kind of reasoning:

1. While more than two super-vertices remain, pick an edge uniformly at random and **contract** it (merge its two ends into one).
2. The edges left between the two super-vertices are a cut. Report its size.

A single run finds a particular minimum cut `C` of size `c` if no edge of `C` is ever contracted. With `k` super-vertices left, every vertex has degree at least `c` (else a smaller cut exists), so there are at least `kc/2` edges and the next contraction hits `C` with probability at most `2/k`. Multiplying `(1 - 2/k)` for `k = n, n-1, ..., 3` gives a success probability of at least `2 / (n(n-1))`. Repeating `n(n-1)/2 * ln n` times fails with probability at most `1/n`.

Giving every edge a random rank and merging in rank order (Kruskal) contracts in a uniformly random order, so the code is a shuffle and a union-find.

```cpp
// Monte Carlo minimum cut (Karger): contract random edges until two super-vertices remain; the edges between them are a cut.
// One run finds a fixed minimum cut with probability at least 2 / (n (n - 1)). Repeat and keep the smallest.
// Giving every edge a random rank and merging in that order (Kruskal) contracts edges in a uniformly random order.
struct Dsu {
    vector<int> p;
    explicit Dsu(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
    bool unite(int a, int b) { a = find(a); b = find(b); if (a == b) return false; p[a] = b; return true; }
};
inline int karger_once(int n, const vector<pair<int, int>>& edges, Rng& rng) {      // the graph must be connected
    vector<int> order(edges.size());
    std::iota(order.begin(), order.end(), 0);
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[rng.below(i)]);
    Dsu d(n);
    int comps = n;
    for (int idx : order) {
        if (comps == 2) break;
        if (d.unite(edges[idx].first, edges[idx].second)) --comps;
    }
    int cut = 0;
    for (const auto& e : edges) if (d.find(e.first) != d.find(e.second)) ++cut;
    return cut;
}
inline int karger(int n, const vector<pair<int, int>>& edges, int trials, Rng& rng) {
    int best = (int)edges.size();
    for (int t = 0; t < trials; ++t) best = std::min(best, karger_once(n, edges, rng));
    return best;
}
```

```
T5 two cliques of 8 joined by 2 edges (16 vertices, 58 edges, minimum cut 2): one run finds it 0.390 of the time (proven lower bound 2/(n(n-1)) = 0.0083); with 333 runs per answer, 300 of 300 answers were right
```

The proven bound for one run is 0.0083 and the measured rate on this graph is 0.390 (two dense lumps joined by two edges is a favourable case: most random edges are inside a lump). With 333 runs per answer, all 300 answers were right. The time for 333 runs is:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| 333 runs of `karger_once` (58 edges) | 0.3 / 0.3 |

The answer is **one-sided**: any cut found is a real cut, so the result is never below the true minimum; it can only be too large. (Karger and Stein's refinement shares work between runs; from memory.)

## 6. Approximation algorithms

An algorithm for a minimisation problem is a **`c`-approximation** if it always returns a solution of cost at most `c` times the optimum (and it runs in polynomial time). The proof needs a lower bound on the optimum that the algorithm can compute without solving the problem.

### 6.1 Vertex cover: take both ends of a matching edge

Scan the edges. If neither end of an edge is chosen yet, choose **both** ends. The chosen edges share no vertex (a **maximal matching**), so any cover needs a different vertex for each of them: `opt >= matching size`. The algorithm returns `2 * matching size`, hence at most `2 * opt`. It is also a cover, because an edge skipped had an end already chosen.

### 6.2 Set cover: take the set that covers the most new elements

Given a universe and a family of sets, choose the fewest sets whose union is the universe. The greedy rule is to repeatedly choose the set that covers the most elements still uncovered. Its result is at most `H(d)` times the optimum, where `d` is the size of the largest set (about `ln d + 1`; the proof charges each element a price equal to `1 / (number of new elements covered by its chosen set)`). From memory, no polynomial algorithm does much better than `ln n` unless P equals NP, so the greedy rule is close to the best possible.

```cpp
// Vertex cover, 2-approximation: take both ends of an edge that no chosen vertex touches (a maximal matching).
// The chosen edges share no vertex, so any cover needs one vertex per chosen edge: opt >= matching size, and we take 2 * matching.
inline vector<int> vertex_cover_2approx(int n, const vector<pair<int, int>>& edges) {
    vector<char> in(n, 0);
    for (const auto& e : edges) if (!in[e.first] && !in[e.second]) in[e.first] = in[e.second] = 1;
    vector<int> cover;
    for (int v = 0; v < n; ++v) if (in[v]) cover.push_back(v);
    return cover;
}
inline bool is_vertex_cover(int n, const vector<pair<int, int>>& edges, const vector<int>& cover) {
    vector<char> in(n, 0);
    for (int v : cover) in[v] = 1;
    for (const auto& e : edges) if (!in[e.first] && !in[e.second]) return false;
    return true;
}
// Greedy set cover: repeatedly take the set that covers the most elements not yet covered (first such set on ties).
// The size is at most H(d) times the optimum, where d is the largest set and H(d) = 1 + 1/2 + ... + 1/d (about ln d + 1).
inline vector<int> greedy_set_cover(int universe, const vector<vector<int>>& sets) {
    vector<char> covered(universe, 0);
    int left = universe;
    vector<int> chosen;
    while (left > 0) {
        int best = -1, gain = 0;
        for (size_t s = 0; s < sets.size(); ++s) {
            int g = 0;
            for (int x : sets[s]) g += !covered[x];
            if (g > gain) { gain = g; best = (int)s; }
        }
        if (best < 0) return {};                            // the sets do not cover the universe
        chosen.push_back(best);
        for (int x : sets[best]) if (!covered[x]) { covered[x] = 1; --left; }
    }
    return chosen;
}
```

Vertex cover first: the 2-approximation against the exact optimum (found by trying all subsets) on random graphs with 18 vertices.

```
T6 vertex cover, matching-based 2-approximation against the exact optimum, 18 vertices, 300 random graphs per row
T6 edge probability 10%: average size / optimum 1.782, worst 2.000, exactly optimal in 0 of 300
T6 edge probability 20%: average size / optimum 1.598, worst 2.000, exactly optimal in 0 of 300
T6 edge probability 40%: average size / optimum 1.375, worst 1.800, exactly optimal in 0 of 300
T6 complete bipartite graph with 9 + 9 vertices: the matching takes 18 vertices, the optimum is 9 (one whole side), ratio 2.0
```

The ratio never exceeds 2, as proved, and in practice it is 1.4 to 1.8 on these graphs. The complete bipartite graph is the instance where the bound is tight: the matching takes both sides, twice the optimum. It was exactly optimal on none of the 900 random graphs: it always returns an even number of vertices and takes both ends of every matching edge.

For set cover, the bound `H(d)` is also tight in the worst case. In the family below, the elements form two rows, and the optimum is the two rows. Greedy is lured away by sets that take a doubling number of elements from both rows: each looks better than a row (which has one element fewer), so it chooses `k` sets for `k` blocks.

```
T7 greedy set cover on the family where the optimum is two sets (top row and bottom row); k blocks of doubling size:
T7 k=2: 6 elements, greedy takes 2 sets, optimum 2, ratio 1.0, H(largest set) = 2.08
T7 k=3: 14 elements, greedy takes 3 sets, optimum 2, ratio 1.5, H(largest set) = 2.72
T7 k=4: 30 elements, greedy takes 4 sets, optimum 2, ratio 2.0, H(largest set) = 3.38
T7 k=5: 62 elements, greedy takes 5 sets, optimum 2, ratio 2.5, H(largest set) = 4.06
T7 k=6: 126 elements, greedy takes 6 sets, optimum 2, ratio 3.0, H(largest set) = 4.74
T7 k=7: 254 elements, greedy takes 7 sets, optimum 2, ratio 3.5, H(largest set) = 5.43
T7 k=8: 510 elements, greedy takes 8 sets, optimum 2, ratio 4.0, H(largest set) = 6.12
T7 k=9: 1022 elements, greedy takes 9 sets, optimum 2, ratio 4.5, H(largest set) = 6.82
T7 k=10: 2046 elements, greedy takes 10 sets, optimum 2, ratio 5.0, H(largest set) = 7.51
T7 random instances (30 elements, 16 sets, each element in a set with probability 0.2), 200 of them: greedy size / optimum average 1.071, worst 1.400
```

The ratio grows as `k/2`, with the number of elements `2^(k+1) - 2`, so the ratio grows like `(log2 n) / 2`, in line with the `ln n` bound. On random instances greedy is far from this worst case: 1.07 on average, 1.40 at worst. A bound is not a prediction.

## 7. Heuristics: local search and simulated annealing for maximum cut

**Maximum cut:** split the vertices into two sides to maximise the number of crossing edges. It is NP-hard (from memory). Three methods, in order of effort:

1. **Random split.** Each edge crosses with probability 1/2, so the expected cut is `m/2`, at least half the optimum (the optimum is at most `m`). A 2-approximation in expectation. It can be derandomised by the method of conditional expectations (fix the vertices one at a time to the side that keeps the expectation highest; from memory).
2. **Local search.** Start anywhere and move a vertex to the other side while it has more neighbours on its own side than across. At the end every vertex has at least half of its edges crossing, so at least `m/2` cross overall, **guaranteed**. Each move raises the cut by at least 1, so there are at most `m` moves. The result is a *local* optimum, not the best.
3. **Simulated annealing.** Flip a random vertex. If the cut does not shrink, accept. If it shrinks by `delta`, accept with probability `exp(-delta / T)`, where the temperature `T` falls geometrically from `t0` to `t1`. At high `T` almost anything is accepted and the search roams; at low `T` it behaves like local search. Accepting a loss now and then is what lets it climb out of a local optimum. The best cut seen is returned.

```cpp
// Maximum cut: split the vertices into two sides to maximise the edges that cross. NP-hard.
struct CutGraph {
    int n;
    vector<pair<int, int>> edges;
    vector<vector<int>> adj;
    CutGraph(int nn, vector<pair<int, int>> e) : n(nn), edges(std::move(e)), adj(nn) {
        for (const auto& x : edges) { adj[x.first].push_back(x.second); adj[x.second].push_back(x.first); }
    }
};
inline int cut_value(const CutGraph& g, const vector<char>& side) {
    int c = 0;
    for (const auto& e : g.edges) c += side[e.first] != side[e.second];
    return c;
}
// A uniformly random split cuts each edge with probability 1/2, so it cuts m/2 edges in expectation.
// Local search: move a vertex to the other side while it has more neighbours on its own side than across.
// At the end every vertex has at least half of its edges crossing, so at least m/2 edges cross in total.
inline void local_search_maxcut(const CutGraph& g, vector<char>& side) {
    for (bool improved = true; improved;) {
        improved = false;
        for (int v = 0; v < g.n; ++v) {
            int same = 0;
            for (int u : g.adj[v]) same += side[u] == side[v];
            if (2 * same > (int)g.adj[v].size()) { side[v] ^= 1; improved = true; }
        }
    }
}
// Simulated annealing: flip a random vertex; keep an improvement always, keep a loss of `delta` with probability exp(-delta / T);
// T falls geometrically from t0 to t1. Returns the best cut seen.
inline int anneal_maxcut(const CutGraph& g, vector<char>& side, long long iters, double t0, double t1, Rng& rng) {
    int cur = cut_value(g, side), best = cur;
    vector<char> best_side = side;
    for (long long it = 0; it < iters; ++it) {
        double T = t0 * std::pow(t1 / t0, (double)it / (double)iters);
        int v = (int)rng.below(g.n), same = 0;
        for (int u : g.adj[v]) same += side[u] == side[v];
        int gain = same - ((int)g.adj[v].size() - same);          // change in the cut if v flips
        if (gain >= 0 || rng.unit() < std::exp(gain / T)) {
            side[v] ^= 1; cur += gain;
            if (cur > best) { best = cur; best_side = side; }
        }
    }
    side = best_side;
    return best;
}
```

On 200 random graphs with 16 vertices (small enough to find the optimum by trying all `2^15` splits):

```
T8 maximum cut on 200 random graphs with 16 vertices and edge probability 0.5, cut / optimum (average, worst): random split 0.726, 0.357; local search 0.948, 0.784; best of 10 local searches 0.996; annealing (20000 flips) 1.000, 1.000
```

A random split averages 0.73 of the optimum with a worst case of 0.36, local search 0.95 (worst 0.78), the best of ten local searches 0.996, and annealing found the optimum in every instance. That last result needs a caveat: annealing used 20,000 flips on 16 vertices, about 1,250 per vertex, which is a very generous budget at this size. On larger graphs the optimum is unknown, so compare costs:

```
T8 300 vertices, 1344 edges (m): random split averages 1.001 * m/2 over 2000 tries; local search 0.685 * m; annealing (400000 flips) 0.742 * m
```

A random split gives `1.001 * m/2`, as the expectation says. Local search reaches `0.685 m` and annealing, with 400,000 flips, `0.742 m`. The optimum is unknown, but it is at least the best of these. Finally, a case with a known optimum: bipartite graphs, where one side against the other cuts every edge.

```
T8 100 random bipartite graphs (15 + 15 vertices, so the optimum cuts every edge): local search reaches it in 84, annealing (60000 flips) in 100
```

| Work | Time (ms), run 1 / run 2 |
|---|---|
| annealing, 60,000 flips, 30 vertices | 2.20 / 3.14 |

Local search gets stuck on 16 of 100 graphs; annealing never did here. That is the pattern: the guarantee belongs to local search (`m/2`), and the better results to annealing, which has no guarantee and needs a schedule that suits the problem (the values `t0 = 2`, `t1 = 0.05` fit because a flip changes the cut by small integers; with other costs you rescale). Treat the schedule and the number of flips as parameters to tune and report, not as constants.

## 8. What to say in an interview

1. **Name the guarantee:** "always correct, expected `O(n log n)`" (Las Vegas) or "wrong with probability at most `2^-30`" (Monte Carlo, one-sided). Never say only "it is randomised".
2. **State the source of randomness:** the coins, not the input. Uniform means unbiased (`rand() % n` is not, in general).
3. **Explain amplification** when the error is one-sided: repeat, multiply the failure probabilities.
4. **For an NP-hard optimisation**, say which of three things you offer: a proven ratio (matching for vertex cover, greedy for set cover), a guaranteed floor without optimality (local search `m/2`), or a heuristic with no proof (annealing), and say how you would measure it (compare with the optimum on small cases, with the best known on large).
5. **Do not use randomness where a deterministic linear algorithm exists** unless the simplicity wins; and in a hard real-time system a Las Vegas algorithm's tail may be unacceptable.
6. **Reproducibility:** log the seed.

## 9. Complexities

| Method | Time | Guarantee |
|---|---|---|
| Randomised quicksort | `O(n log n)` expected, `O(n^2)` worst, tiny probability | Las Vegas |
| Quickselect | `O(n)` expected | Las Vegas |
| Freivalds, `k` rounds | `O(k n^2)` | error `<= 2^-k`, one-sided |
| Miller-Rabin, `k` bases | `O(k log n)` multiplications modulo `n` | error `<= 4^-k`, one-sided |
| Karger, one run | `O(m)` plus the shuffle | success `>= 2/(n(n-1))` |
| Karger, `n^2 ln n / 2` runs | `O(n^2 m log n)` | error `<= 1/n` |
| Vertex cover, matching | `O(n + m)` | `<= 2 * opt` |
| Greedy set cover | `O(total size * answer)` as written | `<= H(d) * opt` |
| Max cut, local search | `O(m)` moves, each `O(deg)` | `>= m/2` |
| Max cut, annealing | `O(iterations * deg)` | none |

## 10. Common mistakes

1. Calling an algorithm "random" and giving no guarantee; mixing up Las Vegas and Monte Carlo.
2. **Amplifying a two-sided or wrongly-sided error by repetition without checking which side is certain.** For Miller-Rabin, "composite" is certain; "prime" is not.
3. Taking one base (or the same base) in Miller-Rabin, or testing only small bases for large `n`.
4. A modulo bias (`rand() % n`), or reusing a generator across threads without thought.
5. Using a randomised quicksort on data with many equal keys and a partition scheme that sends them all to one side: the comparisons become quadratic regardless of the pivot.
6. Overflow in `a * b % m` for 64-bit moduli (use a 128-bit product).
7. In Karger's algorithm, contracting an edge whose ends are already merged (counting a merge that did not happen), or running it on a disconnected graph.
8. Quoting the 2-approximation of TSP or vertex cover as if it were the typical result (the typical ratio is better; the bound is a worst case).
9. Stating that a heuristic is "within X percent of the optimum" after testing on instances where every method reaches the optimum with no effort.
10. Hard-coding the annealing schedule for a problem whose cost changes are on a different scale.

## 11. Questions to answer out loud

1. Las Vegas against Monte Carlo: give an example of each. Which has a random running time?
2. Why does a random pivot beat every input? What is the expected number of comparisons and where does it come from?
3. How would you make a Las Vegas algorithm into a Monte Carlo one?
4. Why is Freivalds' error one-sided? Why is a single wrong entry the worst case?
5. A Carmichael number passes Fermat's test for all coprime bases. What does Miller-Rabin look at that Fermat does not?
6. How many bases do you need for a 64-bit number, and for a number you must accept with error `2^-80`?
7. Walk through Karger's argument that one run finds a minimum cut with probability at least `2 / (n(n-1))`.
8. Prove that the matching-based vertex cover is a 2-approximation. Give an instance where the ratio is exactly 2.
9. Why does greedy set cover have an `H(d)` ratio, and what does the doubling family show?
10. Why is a local optimum of the max-cut local search at least `m/2`?
11. What does the temperature do in simulated annealing? What happens if it is too high or falls too fast?
12. Your boss asks for the shortest tour through 10,000 points. What do you offer, with which guarantee?

## 12. Problems

### 12.1 On paper

**A.** Randomised quicksort with distinct keys: give the expected number of comparisons for `n` = 2, 3, 4 and 5 using `C(n) = (n-1) + (1/n) * sum of (C(i) + C(n-1-i))`. Check `n = 4` against `2(n+1)H(n) - 4n`.

**B.** Freivalds with `A = [[1,2],[3,4]]`, `B = [[5,6],[7,8]]` and the claimed product `C = [[19,22],[43,51]]` (one entry is wrong). Try all four vectors with entries 0 and 1. Which ones expose the error? What fraction of the vectors does not?

**C.** Run Miller-Rabin with base 2 on `n = 561` by hand: write `560 = d * 2^r`, then compute `2^d` mod 561 and square three times. Does the sequence prove 561 composite, and why?

**D.** Karger's algorithm on the graph with edges 0-1, 1-2, 0-2 and 0-3 (a triangle with a pendant vertex). The minimum cut has size 1. Contract edges in a uniformly random order until two super-vertices remain. What is the probability that a run finds the cut of size 1? Compare with `2 / (n(n-1))`.

**E.** Greedy set cover on the doubling family with `k = 3`: the universe is two rows of seven elements. The sets are `T0` (one element from each row), `T1` (two from each), `T2` (four from each), the top row and the bottom row. Which sets does greedy choose, how many, and what is the optimum?

**F.** Classify each as Las Vegas or Monte Carlo, and say which side of the error is certain if Monte Carlo: (i) randomised quicksort; (ii) Freivalds' check; (iii) Miller-Rabin; (iv) Karger's minimum cut; (v) quickselect; (vi) estimating the fraction of items with a property by sampling 1,000 of them.

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | How to approach |
|---|---|---|
| Kth Largest Element in an Array | 215 | Quickselect (section 2) with a random pivot; compare with a heap |
| Sort an Array | 912 | Randomised quicksort, but watch equal keys (a three-way partition) |
| Shuffle an Array | 384 | Fisher-Yates: swap position `i` with a uniform position in `0..i`; why the naive "swap with any position" is biased |
| Random Pick with Weight | 528 | Prefix sums and a binary search on a uniform number |
| Implement Rand10() Using Rand7() | 470 | Rejection sampling: combine two calls into a uniform number in 1..49 and reject the rest |
| Generate Random Point in a Circle | 478 | Rejection from the bounding square, or a radius of `R * sqrt(u)` |
| Insert Delete GetRandom O(1) | 380 | A vector plus a hash map of positions; swap the removed element with the last |
| Linked List Random Node | 382 | Reservoir sampling (13.02) |
| Count Primes | 204 | A sieve; contrast with testing one number by Miller-Rabin |

### 12.3 In your editor

**A.** Test the uniformity of `Rng::below` and of a Fisher-Yates shuffle with a chi-square statistic on small `n`; then write the biased shuffle and see the statistic explode.

**B.** Write **Pollard's rho** factoring (from memory it finds a factor in about `n^(1/4)` steps) using `is_prime` to stop, and factor random 60-bit numbers.

**C.** Implement simulated annealing for the **travelling salesman** with 2-opt moves and compare it with the nearest neighbour plus 2-opt of 14.01 on 100 random cities. Report the tour length, the number of moves and the time.

**D.** Use Karger's algorithm to find a minimum cut of a random graph with 200 vertices, and compare the answer with a deterministic minimum cut (Stoer-Wagner, from the graphs area, if you have written it).

**E.** Implement the **method of conditional expectations** for maximum cut (fix vertices one by one to the side that keeps the expected cut highest) and check that it always reaches at least `m/2`.

### Answers

The program that checks the paper problems printed:

```
A expected comparisons with a random pivot: n=2 1.0000, n=3 2.6667, n=4 4.8333, n=5 7.4000
A formula 2(n+1)H(n) - 4n at n=4: 4.8333
B true product [19 22; 43 50]; vectors: (0,0) same; (0,1) caught; (1,0) same; (1,1) caught; fooled 2 of 4
C 561-1 = 35 * 2^4; 2^35, then squared three times, mod 561: 263 166 67 1; Fermat 2^560 mod 561 = 1; miller_rabin_passes(561,2) = 0
D orders 24, finding the cut of size 1: 12, probability 0.5000 (bound 2/(n(n-1)) = 0.1667)
E k=3: 14 elements, greedy picks sets (by index in the list T2, T1, T0, top, bottom): 0 1 2 , size 3; the optimum is the last two sets, size 2
```

**A.** `C(2) = 1`, `C(3) = 8/3`, `C(4) = 29/6`, `C(5) = 37/5`. For `n = 4` the formula gives `2 * 5 * (25/12) - 16 = 29/6`, which matches.

**B.** The true product is `[[19,22],[43,50]]`. The vectors (0,0) and (1,0) give the same answer on both sides (the wrong entry is in the second column of the second row, so a vector with a zero in that coordinate cannot see it). The vectors (0,1) and (1,1) expose it: for (0,1), `A(Bx) = (22, 50)` and `Cx = (22, 51)`. Half of the vectors miss it, which is the bound of 1/2.

**C.** `560 = 35 * 2^4`. The sequence is `2^35 = 263`, then 166, 67, 1 (mod 561). It never shows 1 first and never shows 560 = -1, so base 2 is a witness: 561 is composite. Indeed 67 is a square root of 1 that is neither 1 nor -1, and a prime has none. Fermat's test, on the other hand, computes `2^560 mod 561 = 1` and accepts.

**D.** The 24 orders of the four edges are all equally likely. A run stops after two merges. The bridge 0-3 always merges when it comes up, so the run fails exactly when the bridge is among the first two edges; it succeeds when the bridge is third or fourth, in 12 of the 24 orders: probability 1/2. The proven bound is `2/(4 * 3) = 1/6`, so the bound is loose here.

**E.** Greedy first takes `T2` (8 elements, more than a row's 7), then `T1`, then `T0`: 3 sets. The optimum is the two rows, 2 sets. In general it takes `k` sets against 2.

**F.** (i) Las Vegas. (ii) Monte Carlo, one-sided: "different" is always right, "equal" can be wrong. (iii) Monte Carlo, one-sided: "composite" is always right, "prime" can be wrong. (iv) Monte Carlo, one-sided in the sense that the cut found is real and never smaller than the minimum, but may be larger. (v) Las Vegas. (vi) Monte Carlo, two-sided: the estimate can err in either direction.

## Cheat sheet

- **Las Vegas:** always right, random time (quicksort, quickselect). **Monte Carlo:** random answer, bounded time. Repeat a one-sided test `k` times: error `p^k`.
- **Random pivot:** exactly `2(n+1)H(n) - 4n` comparisons for distinct keys (measured 39,499 against 39,520 at `n = 3000`); a sorted array costs 4,498,500 with the first element as pivot and 38,026 with a random one.
- **Freivalds:** compare `A(Bx)` with `Cx`; error at most 1/2 per round (measured 0.4996 with one wrong entry).
- **Miller-Rabin:** `n - 1 = d 2^r`; at most a quarter of the bases are liars (measured average 0.003); twelve prime bases are exact for 64-bit; Fermat is fooled by Carmichael numbers.
- **Karger:** contract random edges to two vertices; one run succeeds with probability at least `2/(n(n-1))` (measured 0.390 against 0.0083 on a favourable graph); `n^2 ln n / 2` runs give error at most `1/n`.
- **Vertex cover:** take both ends of a maximal matching, at most 2 times the optimum (tight on complete bipartite graphs). **Set cover:** greedy, at most `H(d)` times the optimum (the doubling family gives `k/2`).
- **Max cut:** random `m/2` in expectation; local search at least `m/2`, guaranteed; annealing best in practice, no guarantee (it reached the optimum on 100 of 100 bipartite graphs, local search on 84).
- **Say in an interview:** name the guarantee, say what is random, say how to lower the error, say how you would measure a heuristic.

---

## Sources

- All code is tested here (1,744,300 checks under ASan and UBSan; 24 deliberate bugs all caught, two only after tests were added). Traces and timings are measured on one Xeon container, two runs each, with identical traces. Graphs and matrices are random and seeded.
- The theory (the exact expectation of quicksort, the 3.4 n constant for quickselect, the one-quarter bound on Miller-Rabin liars, the fact that twelve prime bases are exact below 3.3 * 10^24, Karger's success probability and the `ln n` hardness of set cover, the NP-hardness of maximum cut, the existence of Karger-Stein, the method of conditional expectations, Pollard's rho cost) is standard material and was written from memory. The expectation of quicksort, the Freivalds bound, the 2-approximation of vertex cover, the `m/2` bound of local search and the Karger bound were also reasoned out here and tested against the measured data.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
