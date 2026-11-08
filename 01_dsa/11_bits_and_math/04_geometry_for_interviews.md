# DSA 11.04: Geometry for Interviews

*Part of the [Bits and math plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Number theory and arithmetic](02_number_theory_and_arithmetic.md) (gcd, used to normalise directions), [Binary search and sorting](../03_binary_search_and_sorting/05_sorting_patterns_and_comparators.md) (custom comparators), [Heaps](../07_trees/15_binary_heaps.md) (the k-closest heap), and C 01 (why the cross product needs a wider type)*

Interview geometry is mostly **integer** geometry: points with whole-number coordinates, and questions that ask "is this a left turn?", "do these segments touch?", "what is the area?", "which points are on the hull?", "which pair is closest?". Almost every one of them is solved by a single operation, the **cross product**, used exactly, with no square roots, no angles and no division. This tutorial builds that toolbox and tests each piece against a brute force.

By the end you can:

- compute the orientation of three points exactly and say why floating point gets it wrong;
- test whether two segments intersect, including touching and collinear cases;
- compute a polygon's area (shoelace), test a point against any polygon, and use Pick's theorem to count lattice points;
- build a convex hull with Andrew's monotone chain, with or without collinear points;
- test rectangle overlap, find the most points on one line without slopes, and sort points by angle without trigonometry;
- find the `k` closest points in `O(n)` average time, and the closest pair of points in `O(n log n)`.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 3,231,272 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran: `orient` against a direct comparison of the two products on 400,000 random triples (including coordinates at plus or minus 10^9) and against its symmetries, plus the Fibonacci triples of section 2; `segments_intersect` against an independent parametric test on **every one of the 65,536 segment pairs on a 4 by 4 grid** (which contains all the collinear and single-point cases) and on 300,000 random pairs; `area2` against the Heron-type identity `4 (2A)^2 = 4 |ab|^2 |ac|^2 - (|ab|^2 + |ac|^2 - |bc|^2)^2` on 100,000 triangles; `point_in_polygon` and `area2` together against **Pick's theorem** on 3,000 random star-shaped polygons and on random hulls; `angle_less` against `atan2`; `convex_hull` against the definition ("a vertex is a point that lies in no triangle of the other points") on 20,000 small random sets full of duplicates and collinear points, and for convexity on 300 sets of up to 2,000 points; rectangles against unit-cell counting on 100,000 pairs; `max_points_on_line` against an `O(n^3)` loop on 30,000 sets with duplicates; both `k_closest` versions against a full sort on 80,000 cases; and `closest_pair_dist2` against the `O(n^2)` loop on 6,000 sets, including all points on one vertical or horizontal line. I also broke the code on purpose in four ways to check that the tests notice: three were caught, and the fourth (changing the half-open rule in `point_in_polygon` from `>` to `>=`) is an equivalent rule, so passing was the right result.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. The model: integer points and four rules

A point is a pair of integers. The rules that keep the answers exact:

1. **Compare squared distances.** `dx*dx + dy*dy` is an integer; the square root is not, and you never need it to compare.
2. **Never divide.** A slope `dy/dx` is a fraction; compare fractions by cross-multiplying, or reduce the direction `(dx, dy)` by its gcd.
3. **Never use angles.** `atan2` returns a rounded number. Order directions by the sign of a cross product instead.
4. **Know your overflow budget.** With every coordinate at most `10^9` in absolute value, a difference is below `2 * 10^9`, a product below `4 * 10^18` (fits in a 64-bit signed number, which holds about `9.2 * 10^18`), and a cross product, the **difference of two products**, can reach `8 * 10^18`, close to the limit. The code holds it in 128 bits so there is no question.

**Orientation of the axes.** All the pictures here use the usual maths convention: `x` to the right, `y` up, so "counter-clockwise" means a left turn. On a screen where `y` points down, a counter-clockwise result looks clockwise; the algebra is unchanged, only the word you say flips.

```cpp
// Exact 2D geometry on integer points.  Precondition for everything below: |x|, |y| <= 10^9, so every difference fits in 32 bits,
// every product fits in 64 bits, and the cross product (a difference of two products) fits in 65 bits: we hold it in 128 bits.
struct Pt { ll x, y; };
inline Pt operator-(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
inline bool operator==(Pt a, Pt b) { return a.x == b.x && a.y == b.y; }
inline bool operator<(Pt a, Pt b) { return a.x != b.x ? a.x < b.x : a.y < b.y; }
inline i128 cross(Pt a, Pt b) { return (i128)a.x * b.y - (i128)a.y * b.x; }     // a.x*b.y - a.y*b.x: twice the signed area of the triangle (0, a, b)
inline ll dot(Pt a, Pt b) { return a.x * b.x + a.y * b.y; }
inline ll dist2(Pt a, Pt b) { Pt d = a - b; return d.x * d.x + d.y * d.y; }        // squared distance: no square root, stays an integer
inline int sgn(i128 v) { return (v > 0) - (v < 0); }
// Orientation of the turn a -> b -> c:  +1 left (counter-clockwise), 0 the three points are collinear, -1 right (clockwise).
inline int orient(Pt a, Pt b, Pt c) { return sgn(cross(b - a, c - a)); }
```

## 2. The cross product and orientation

The **cross product** of two vectors `a = (ax, ay)` and `b = (bx, by)` is the number `ax*by - ay*bx`. Its meaning:

- Its absolute value is the area of the parallelogram spanned by `a` and `b`; half of it is the area of the triangle.
- Its **sign** tells which way you turn going from `a` to `b`: positive for counter-clockwise (left), negative for clockwise (right), zero if the vectors are parallel.

For three points, the turn `a -> b -> c` is the sign of `cross(b - a, c - a)`; that is `orient(a, b, c)`. From the program, with `a = (0,0)` and `b = (4,0)`:

```
T1 orient((0,0),(4,0),c): c=(2,3) -> 1, c=(2,-3) -> -1, c=(7,0) -> 0; cross((4,0),(2,3)) = 12, twice the area of the triangle
```

(`(2, 3)` is above the line from `(0,0)` to `(4,0)`, a left turn; `(2, -3)` is below, a right turn; `(7, 0)` is on the line.) The value 12 is twice the area of the triangle `(0,0), (4,0), (2,3)`, whose base is 4 and height 3.

Three consequences you will use constantly: `orient(a, b, c) == 0` is the exact test for **collinear**; swapping two points flips the sign; and cycling the three points (`a, b, c` to `b, c, a`) keeps it.

**Why not floating point?** The sign is the only thing you want, and for nearly collinear points the two products are almost equal, so the subtraction cancels all the correct digits and leaves rounding error. A `double` holds 53 bits (integers exactly up to about `9 * 10^15`), but products of 9-digit coordinates reach `10^17`. Fibonacci numbers give a clean demonstration, because of Cassini's identity: `F(n+1) F(n-1) - F(n)^2 = (-1)^n`. So the points `b = (F(n+1), F(n))` and `c = (F(n), F(n-1))` have cross product exactly plus or minus 1, from huge products. The program computes the cross product in three ways:

```
T2 b=(701408733,433494437), c=(433494437,267914296): exact cross = -1; in double the same formula gives 0; in long double -1; wrong signs for n=1..43: double 3 of 43, long double 0 of 43
```

So with coordinates below `10^9` the plain `double` formula returns the wrong sign (here, 0, "collinear") in 3 of the 43 cases; `long double` (64 bits of mantissa on this x86 machine) was right in all 43, but only because the products stay below `2^64`, and it is not guaranteed on every platform (on some compilers `long double` is the same as `double`). Integers in 128 bits are exact, and are the safe default.

## 3. Do two segments intersect?

Segments `ab` and `cd` **cross properly** when `c` and `d` are strictly on opposite sides of the line through `ab`, **and** `a` and `b` are strictly on opposite sides of the line through `cd`. Opposite sides means the two orientations have opposite signs: their product is negative.

That misses the cases where the segments only **touch** or overlap: an endpoint of one lies on the other, which includes collinear segments that overlap. So add: some endpoint is collinear with the other segment **and** inside its bounding box (`on_segment`). Nothing else can happen.

```cpp
// Is p on the closed segment ab?  (Collinear, and inside the bounding box of the segment.)
inline bool on_segment(Pt a, Pt b, Pt p) {
    return orient(a, b, p) == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x)
                                && std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
}
// Do the closed segments ab and cd share at least one point?  Works when a segment is a single point and when segments are collinear.
// Proper crossing: c and d on strictly opposite sides of line ab AND a and b on strictly opposite sides of line cd.  Everything else
// that counts is a touch: some endpoint lies on the other segment.
inline bool segments_intersect(Pt a, Pt b, Pt c, Pt d) {
    int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    return (o1 == 0 && on_segment(a, b, c)) || (o2 == 0 && on_segment(a, b, d))
        || (o3 == 0 && on_segment(c, d, a)) || (o4 == 0 && on_segment(c, d, b));
}
```

The seven cases for the segment `(0,0)-(4,4)`:

```
T3 segment (0,0)-(4,4) against: [X crossing: yes] [T touch at an endpoint: yes] [collinear, overlapping: yes] [collinear, disjoint: no] [parallel: no] [a point on the segment: yes] [a point off the segment: no]
```

A segment that is a single point (`c == d`) is handled by the same code: both orientations of that point are tested against `ab`, and `on_segment(a, b, c)` decides. Because every comparison is an exact integer sign, nothing special is needed for parallel lines, and there is no division by a zero determinant to guard.

**Where do they meet?** If you need the intersection point (not just a yes or no), it is a rational number: `a + t (b - a)` with `t = cross(c - a, d - c) / cross(b - a, d - c)`. Keep the numerator and denominator separate as integers, and only convert at the end if the problem demands it.

## 4. Polygons

### 4.1 Area: the shoelace formula

For vertices `p0, p1, ..., p(n-1)` in order, the area is half of `sum of cross(p_i, p_(i+1))` (indices wrap around). The sum splits the polygon into triangles fanned from the origin, and the triangles on the "outside" of an edge count negatively, so the pieces outside the polygon cancel. The result is **positive for counter-clockwise** order and **negative for clockwise**. The sum of integer crosses is an integer, and the area may end in `.5`, so the code returns **twice the area**.

```cpp
// Twice the signed area of a polygon given by its vertices in order (shoelace formula): positive for counter-clockwise order.
// Twice the area is an integer; the area itself may end in .5, so keep the doubled value.
inline i128 area2(const std::vector<Pt>& p) {
    i128 s = 0; int n = (int)p.size();
    for (int i = 0; i < n; ++i) s += cross(p[i], p[(i + 1) % n]);
    return s;
}
// Point in a simple polygon (any shape, convex or not): 0 outside, 1 on the boundary, 2 strictly inside.
// Ray casting to the right: count the edges that the horizontal ray from q crosses.  An edge is counted when exactly one endpoint is
// strictly above q's height (a half-open rule, so a vertex exactly at q's height is never counted twice) and q is on the correct side.
inline int point_in_polygon(const std::vector<Pt>& poly, Pt q) {
    int n = (int)poly.size(); bool in = false;
    for (int i = 0; i < n; ++i) {
        Pt a = poly[i], b = poly[(i + 1) % n];
        if (on_segment(a, b, q)) return 1;
        if ((a.y > q.y) != (b.y > q.y)) {
            int o = orient(a, b, q);
            if (b.y > a.y ? o > 0 : o < 0) in = !in;         // q is to the left of the edge as seen from the ray's side
        }
    }
    return in ? 2 : 0;
}
```

From the program:

```
T4 area2: rectangle 4x3 ccw = 24 (area 12), same vertices clockwise = -24, triangle (0,0),(4,0),(0,3) = 12 (area 6.0)
```

### 4.2 Is a point inside a polygon?

For a convex polygon, "to the left of every edge" suffices. For any simple polygon use **ray casting**: shoot a ray from the point to the right and count how many edges it crosses; odd means inside. Two details decide whether the code is correct:

- **Edges that the ray only grazes.** A ray through a vertex would touch two edges at once. The half-open rule fixes it: count an edge only if exactly one endpoint is strictly above the ray's height (`(a.y > q.y) != (b.y > q.y)`). A vertex exactly at the ray's height then counts as "below", consistently for both its edges.
- **Which side.** Among the edges that straddle the ray's height, the ray hits the edge when the point is to the left of it, as seen going upward. That is `orient(a, b, q) > 0` for an upward edge and `< 0` for a downward one.

The function first checks `on_segment` for each edge, so a point on the boundary returns 1 before the counting matters. A concave (L-shaped) polygon, with its notch, from the program:

```
T5 L-shaped polygon (area2 = 24): (1,1) -> 2, (3,1) -> 2, (3,3) -> 0 (the notch), (2,3) -> 1 (on the boundary), (4,2) -> 1 (a vertex), (5,5) -> 0  [0 outside, 1 boundary, 2 inside]
```

The point `(3, 3)` is in the notch, outside the polygon, even though it is inside the bounding box; the crossing count handles that without special cases. Cost: `O(n)` per query.

### 4.3 Pick's theorem

For a simple polygon whose vertices are lattice points, with `I` lattice points strictly inside and `B` lattice points on the boundary, the area is `A = I + B/2 - 1`. In the doubled form that the code uses, `2A = 2I + B - 2`. The boundary count follows from the edges: along an edge with difference `(dx, dy)` there are `gcd(|dx|, |dy|)` lattice steps, so `B` is the sum of those gcds. So from the area and `B` you get `I` without enumerating. For the triangle `(0,0), (4,0), (0,3)`:

```
T7 Pick's theorem on the triangle (0,0),(4,0),(0,3): boundary lattice points B = 8, interior points counted = 3, and 2A = 2I + B - 2 gives 12 = area2 12
```

(Edges give gcds 4, 1, 3, so `B = 8`; `2A = 12`; hence `I = (12 - 8 + 2) / 2 = 3`.) The tests use this theorem as an independent check of `area2` and `point_in_polygon`: they must agree on every polygon.

## 5. Convex hull

The **convex hull** of a set of points is the smallest convex polygon containing them; picture a rubber band stretched around nails. **Andrew's monotone chain**:

1. Sort the points by `x`, then `y`, and drop duplicates.
2. Walk left to right keeping a stack. For each new point, **pop** the top of the stack while the last two points and the new one do not make a strict left turn (`orient <= 0`); then push. At the end the stack is the **lower hull**.
3. Do the same walking right to left for the **upper hull**.
4. Join the two chains (each ends where the other starts, so one point is dropped from each).

The pop condition is the heart: a point that lies on or to the right of the line from the earlier point to the new one is not a vertex of this side of the hull. Using `<= 0` removes collinear points on the edges, giving the minimal vertex set; using `< 0` keeps them (needed when the problem says "include all points on the boundary").

```cpp
// Convex hull, Andrew's monotone chain: sort by (x, y), build the lower hull left to right and the upper hull right to left, popping
// while the last turn is not strictly left.  Returns the vertices counter-clockwise starting from the lowest-leftmost point, WITHOUT
// collinear points on the edges (use `< 0` instead of `<= 0` to keep them).  O(n log n), dominated by the sort.
inline std::vector<Pt> convex_hull(std::vector<Pt> p) {
    std::sort(p.begin(), p.end());
    p.erase(std::unique(p.begin(), p.end()), p.end());          // duplicates would give zero-length edges
    int n = (int)p.size();
    if (n <= 2) return p;
    std::vector<Pt> h(2 * n); int k = 0;
    for (int i = 0; i < n; ++i) {                                // lower hull
        while (k >= 2 && orient(h[k-2], h[k-1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; --i) {                // upper hull
        while (k >= t && orient(h[k-2], h[k-1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    h.resize(k - 1);                                             // the last point repeats the first
    return h;
}
```

An example with collinear boundary points, interior points and a duplicate, and the degenerate case of collinear input:

```
T6 hull of 10 points (collinear boundary points, interior points, a duplicate): 4 vertices: (0,0) (4,0) (4,4) (0,4); four collinear points -> 2 vertices: (0,0) (3,3)
```

The ten input points reduce to the four corners; four collinear points give just the two endpoints, which the code returns correctly (the test suite also covers a single point, a repeated single point and empty input). Each point is pushed once and popped at most once, so the walk is `O(n)` and the **sort dominates: `O(n log n)`**. Measured on 1,000,000 random points with coordinates up to `10^9`: the whole hull took 111.2 / 113.8 ms, of which sorting alone was 77.7 / 87.4 ms (the points are random, so the hull itself is small; a worst case such as all points on a circle would do more pushes).

Uses of the hull: the diameter of a point set (the farthest pair is on the hull), the smallest enclosing rectangle, "does this point lie in the hull of the others", and "the fence" problems.

## 6. Axis-aligned rectangles

Two intervals on a line overlap (with positive length) when `max(left ends) < min(right ends)`. Two rectangles overlap when **both** their x-intervals and their y-intervals do. The test is strict, so rectangles that only share an edge or a corner do not overlap (LeetCode 836 defines it this way; read the problem for its definition). The overlap area is the product of the two overlap lengths, or 0, and the union area follows by inclusion-exclusion (from [11.03](03_combinatorics_probability_and_sampling.md)): `|A| + |B| - |A and B|`.

```cpp
// Axis-aligned rectangles [x1, x2] x [y1, y2] with x1 < x2 and y1 < y2.  Two rectangles overlap (with positive area) iff their
// x-intervals overlap AND their y-intervals overlap, each as an open-interval test: max(left edges) < min(right edges).
struct Rect { ll x1, y1, x2, y2; };
inline bool rects_overlap(const Rect& a, const Rect& b) {
    return std::max(a.x1, b.x1) < std::min(a.x2, b.x2) && std::max(a.y1, b.y1) < std::min(a.y2, b.y2);
}
inline ll overlap_area(const Rect& a, const Rect& b) {
    ll w = std::min(a.x2, b.x2) - std::max(a.x1, b.x1), h = std::min(a.y2, b.y2) - std::max(a.y1, b.y1);
    return (w > 0 && h > 0) ? w * h : 0;
}
inline ll union_area(const Rect& a, const Rect& b) {                // inclusion-exclusion: |A| + |B| - |A and B|
    return (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - overlap_area(a, b);
}
```

```
T8 rectangles: [0,0,2,2] and [1,1,3,3] overlap: yes (area 1); [0,0,2,2] and [2,0,4,2] share only an edge: overlap no (area 0); LeetCode 223 example [-3,0,3,4] and [0,-1,9,2]: union area 45
```

The common slip is forgetting that one rectangle can lie completely inside another, or checking only "does a corner of one lie inside the other", which misses the cross-shaped overlap of a wide thin rectangle and a tall thin one. The interval test has neither problem.

## 7. Points on a line, without slopes

"What is the most points that lie on one line?" (LeetCode 149.) Pick an anchor point; every other point defines a direction from the anchor; points in the same direction (or the exact opposite one) are on one line through the anchor. So count equal directions. The temptation is to use the slope `dy/dx` in a `double`, and it fails: there are points whose slopes differ by less than a `double` can represent.

The fix is to **reduce the direction `(dx, dy)` by the gcd** and fix the sign, so the same line always gives the same pair of integers. A line has two opposite directions, so normalise to a canonical one (for example `dx > 0`, or `dx = 0` and `dy > 0`). Duplicates of the anchor are counted separately because their direction is `(0, 0)`.

```cpp
// Most points on one line (LeetCode 149), O(n^2 log n): from each anchor point, reduce the direction to every other point by the gcd
// and fix its sign, so equal directions are equal pairs; the best line through the anchor is the longest run of one direction plus the
// copies of the anchor.  No division, no slopes, no floating point.
inline int max_points_on_line(const std::vector<Pt>& p) {
    int n = (int)p.size(), best = std::min(n, 2);
    for (int i = 0; i < n; ++i) {
        std::vector<std::pair<ll, ll>> dir; int same = 1;        // `same` counts the anchor and its duplicates
        for (int j = 0; j < n; ++j) if (j != i) {
            ll dx = p[j].x - p[i].x, dy = p[j].y - p[i].y;
            if (dx == 0 && dy == 0) { ++same; continue; }
            ll g = std::gcd(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
            dx /= g; dy /= g;
            if (dx < 0 || (dx == 0 && dy < 0)) { dx = -dx; dy = -dy; }   // a line has two directions: pick one
            dir.push_back({dx, dy});
        }
        std::sort(dir.begin(), dir.end());
        int run = 0;
        for (size_t t = 0; t < dir.size(); ++t) { run = (t && dir[t] == dir[t-1]) ? run + 1 : 1; best = std::max(best, same + run); }
        best = std::max(best, same);
    }
    return best;
}
```

The failure case that breaks slope comparison in `double`, from the program:

```
T9 three points (0,0), (94911151,94911150), (94911152,94911151): slopes in double are 0.99999998946383029 and 0.99999998946383029, equal: yes; exact cross = 1; max_points_on_line = 2
```

The two slopes print identically in `double`, so a slope-based solution says the three points are collinear, but the exact cross product is 1, not 0, and the code above answers 2. A second example, with a duplicate:

```
T10 max_points_on_line for (1,1),(2,2),(3,3),(3,4),(4,5),(5,6),(1,3),(-1,-1),(3,3): 5 (the line y = x holds (1,1),(2,2),(3,3),(-1,-1) and the duplicate (3,3))
```

Cost: `O(n^2 log n)` (a sort per anchor); a hash map of directions makes it `O(n^2)` on average. Measured with 2,000 points on a 50 by 50 grid (so many share lines): 380.5 / 331.2 ms.

## 8. Sorting by angle

To order points around a centre (for example for a sweep, or to build a star-shaped polygon), compare **directions**, not computed angles. Split the plane into two halves, the upper one (including the positive x-axis) and the rest; points in different halves compare by half; inside a half, `a` comes before `b` when `cross(a, b) > 0` (a left turn from `a` to `b`). Points on the same ray compare equal.

```cpp
// Sort points around the origin by angle, counter-clockwise from the positive x-axis, with no trigonometry: first split into the upper
// half-plane (including the positive x-axis) and the rest, then compare inside a half by the sign of the cross product.  The origin
// itself must not be in the input.  Points on the same ray compare equal.
inline int half_plane(Pt p) { return (p.y > 0 || (p.y == 0 && p.x > 0)) ? 0 : 1; }
inline bool angle_less(Pt a, Pt b) {
    int ha = half_plane(a), hb = half_plane(b);
    return ha != hb ? ha < hb : cross(a, b) > 0;
}
```

```
T11 sorted by angle from the positive x-axis, counter-clockwise: (1,0) (3,1) (0,3) (-2,2) (-1,1) (-3,-3) (0,-1) (2,-2)
```

The order starts at the positive x-axis `(1, 0)` and turns counter-clockwise. The comparator is a strict weak ordering, as `std::sort` requires. (A comparator that uses the cross product alone, without the half-plane split, is not transitive over the whole circle, and can crash or corrupt a sort.)

## 9. The `k` closest points

Given `n` points, return the `k` closest to the origin (LeetCode 973). Compare **squared** distances. Three ways:

- **Sort everything**, `O(n log n)`; simple and fine for small inputs.
- **A max-heap of size `k`**, `O(n log k)`: push each point, and pop the farthest whenever the heap exceeds `k`. It also works on a stream.
- **`nth_element`** (quickselect), `O(n)` on average: it rearranges the array so the `k` smallest come first, in no particular order. It is the fastest and needs the data in memory.

```cpp
// The k points closest to the origin (LeetCode 973): nth_element partitions so the k smallest squared distances come first, O(n) on
// average.  The squared distance is exact (no sqrt); the result is in no particular order.
inline std::vector<Pt> k_closest(std::vector<Pt> p, size_t k) {
    Pt o{0, 0};
    std::nth_element(p.begin(), p.begin() + k, p.end(), [&](Pt a, Pt b) { return dist2(a, o) < dist2(b, o); });
    p.resize(k); return p;
}
// The same with a max-heap of size k: O(n log k), and it works on a stream.
inline std::vector<Pt> k_closest_heap(const std::vector<Pt>& p, size_t k) {
    Pt o{0, 0};
    std::priority_queue<std::pair<ll, int>> h;                   // (squared distance, index): the farthest of the k on top
    for (int i = 0; i < (int)p.size(); ++i) {
        h.push({dist2(p[i], o), i});
        if (h.size() > k) h.pop();
    }
    std::vector<Pt> r; while (!h.empty()) { r.push_back(p[h.top().second]); h.pop(); }
    return r;
}
```

```
T12 k_closest of (3,3),(5,-1),(-2,4), k = 2: squared distances 18, 26, 20, result (3,3) (-2,4)
```

Measured on 1,000,000 points with `k = 100`: `nth_element` took 12.3 / 12.3 ms, the heap 22.2 / 22.5 ms, and a full sort 101.6 / 98.0 ms. With ties at the boundary the choice among tied points is arbitrary; the problem statements promise a unique answer.

## 10. The closest pair of points

The obvious answer compares all `n (n-1) / 2` pairs: `O(n^2)`. **Divide and conquer** does it in `O(n log n)`:

1. Sort by `x`; split at the median `x` into a left and a right half and solve each recursively. Let `d` be the smaller of the two answers (as a squared distance).
2. A closer pair that **straddles the dividing line** must have both points within distance `d` of the line. Take those points, in order of `y`.
3. In that strip, a point only needs comparing with the few points just below it in `y`: any point farther than `d` vertically is too far, and in a strip of width `2d` only a constant number of points can sit within `d` vertically of each other without two of them being closer than `d` (the classical argument packs them into a few squares of side `d/2`, giving a bound of about 7 neighbours; I rely on that argument and did not measure it separately; the code stops as soon as the vertical gap is at least `d`).
4. Keep the array sorted by `y` as a merge sort would, so each level costs `O(n)`.

```cpp
// Closest pair of points in O(n log n): split by the median x, solve both halves, then only points within distance d of the dividing
// line can improve on the answer d, and in that strip, sorted by y, each point needs comparing with only a few neighbours.
// Returns the minimum SQUARED distance (0 if two points coincide).  The array is sorted by x on entry and by y on exit (merge sort).
inline ll closest_rec(std::vector<Pt>& a, int l, int r) {
    if (r - l <= 3) {
        ll d = (ll)4e18;
        for (int i = l; i < r; ++i) for (int j = i + 1; j < r; ++j) d = std::min(d, dist2(a[i], a[j]));
        std::sort(a.begin() + l, a.begin() + r, [](Pt u, Pt v) { return u.y < v.y; });
        return d;
    }
    int m = (l + r) / 2; ll midx = a[m].x;
    ll d = std::min(closest_rec(a, l, m), closest_rec(a, m, r));
    std::inplace_merge(a.begin() + l, a.begin() + m, a.begin() + r, [](Pt u, Pt v) { return u.y < v.y; });
    std::vector<Pt> strip;
    for (int i = l; i < r; ++i) {
        ll dx = a[i].x - midx;
        if (dx * dx >= d) continue;                              // too far from the dividing line to beat d
        for (int j = (int)strip.size() - 1; j >= 0; --j) {
            ll dy = a[i].y - strip[j].y;
            if (dy * dy >= d) break;                             // sorted by y: everything earlier is farther still
            d = std::min(d, dist2(a[i], strip[j]));
        }
        strip.push_back(a[i]);
    }
    return d;
}
inline ll closest_pair_dist2(std::vector<Pt> p) {                // needs at least 2 points
    std::sort(p.begin(), p.end());
    return closest_rec(p, 0, (int)p.size());
}
```

```
T13 closest pair of (2,1),(9,9),(0,0),(3,7),(8,8),(5,5),(1,9): squared distance 2 (the pair (9,9),(8,8)); with a repeated point the answer is 0
```

Repeated points give 0. The recurrence is `T(n) = 2 T(n/2) + O(n)`, so `O(n log n)`. Measured: for 20,000 random points the quadratic loop took 175.1 / 158.5 ms and the divide and conquer 4.2 / 3.8 ms; for 1,000,000 points the divide and conquer took 246.1 / 274.6 ms (the quadratic loop would need about 5 * 10^11 pair tests). An alternative is a randomised grid with cell size equal to the current best distance, expected `O(n)`; it is an editor exercise.

## 11. Complexities

| Operation | Time | Space |
|---|---|---|
| `orient`, `on_segment`, `segments_intersect` | `O(1)` | `O(1)` |
| Polygon area | `O(n)` | `O(1)` |
| Point in polygon (one query) | `O(n)` | `O(1)` |
| Convex hull | `O(n log n)` | `O(n)` |
| Rectangle overlap and area | `O(1)` | `O(1)` |
| Most points on a line | `O(n^2 log n)` (`O(n^2)` with hashing) | `O(n)` |
| Sort by angle | `O(n log n)` | `O(1)` extra |
| `k` closest: sort / heap / `nth_element` | `O(n log n)` / `O(n log k)` / `O(n)` average | `O(1)` / `O(k)` / `O(1)` |
| Closest pair, divide and conquer | `O(n log n)` | `O(n)` |

## 12. Which tool for which problem

- "Which side", "is it a turn", "are these collinear", "is the triangle degenerate": `orient`.
- "Do these segments touch / cross": `segments_intersect`. "Does this path cross itself": test pairs.
- "Area of a polygon", "lattice points inside": shoelace and Pick.
- "Is the point inside": ray casting (any polygon) or all-left-turns (convex).
- "Fence around points", "farthest pair", "smallest enclosing shape": convex hull.
- "Rectangle overlap / union": interval overlap on each axis.
- "Points on a line", "same direction": reduce by the gcd.
- "Order around a point": half-plane and cross product.
- "Nearest few": `nth_element` or a heap. "Nearest two": divide and conquer.

## 13. Common mistakes

1. **Using `double` for orientation or equality of slopes** (the Fibonacci and LeetCode 149 cases above).
2. **Multiplying in 32 bits.** A cross product of two `int` differences overflows; widen before multiplying.
3. **Taking a square root to compare distances.** Compare squares.
4. **Forgetting collinear and duplicate points in a hull.** Decide what the problem wants (`<= 0` removes collinear points, `< 0` keeps them), and remove duplicates first.
5. **Treating touching as crossing** (or the opposite) in segment and rectangle tests. Read whether the boundary counts.
6. **Checking only the "proper crossing" case** and missing collinear overlap and endpoint touching.
7. **A ray through a vertex counted twice** in point-in-polygon. Use the half-open rule.
8. **Mixing up the sign convention** (clockwise versus counter-clockwise) between the algebra and a screen with `y` pointing down.
9. **A cross-product-only angle comparator** (not transitive over the full circle).
10. **Reporting "the area" from `area2` without dividing by two**, or dividing an odd `area2` in integer arithmetic.
11. **Expecting the shoelace area to be positive**; it is negative for clockwise order. Take the absolute value if only the size matters.
12. **Comparing a floating-point result to 0 with `==`.** If you must use floats, you need a tolerance, and tolerances introduce their own failures.

## 14. Questions to answer out loud

1. What is the cross product, and what do its absolute value and sign mean?
2. How do you test that three points are collinear? Why is a `double` unsafe?
3. List every case of "two segments intersect". Why do the orientation signs alone miss some?
4. Derive the shoelace formula, and say what happens for clockwise vertices.
5. Explain ray casting and the half-open rule. What goes wrong without it?
6. State Pick's theorem. How do you count the boundary lattice points of an edge?
7. Describe the monotone chain algorithm. Why is it `O(n log n)`, and what does `<= 0` versus `< 0` change?
8. How do you test two rectangles for overlap? Why is it not enough to test corners?
9. How do you find the most collinear points without slopes? What are the two normalisations?
10. How do you sort by angle without `atan2`? Why does the half-plane split matter?
11. Compare three ways to find the `k` closest points, and say when you would use each.
12. Explain the closest-pair algorithm and why the strip step is linear.

## 15. Problems

### 15.1 On paper

Answer first, then run the code to check.

**A.** What is `orient((0,0), (5,1), (2,3))`, and what is the cross product?

**B.** Do the segments `(1,1)-(5,5)` and `(3,3)-(8,8)` intersect? Do `(0,0)-(2,2)` and `(3,3)-(5,5)`?

**C.** What is the area of the polygon `(0,0), (6,0), (6,4), (3,7), (0,4)`?

**D.** For the triangle `(0,0), (6,0), (0,4)`, find the number of boundary lattice points and, from Pick's theorem, the number of interior ones.

**E.** What is the convex hull of `(0,0), (2,1), (4,0), (2,4), (2,2), (1,1)`?

**F.** Do the rectangles `[0,0,3,3]` and `[3,1,5,2]` overlap? What is the union area of `[0,0,4,4]` and `[2,2,6,6]`?

**G.** What is the most points on one line among `(0,0), (1,1), (2,2), (1,0), (2,0), (3,0)`?

**H.** The 2 closest points to the origin among `(1,0), (0,2), (-3,0), (2,2), (0,-1)`?

**I.** The smallest squared distance between two of `(0,0), (10,0), (3,4), (10,1), (7,7)`?

**J.** Sort `(1,0), (0,1), (-1,0), (0,-1), (1,1)` by angle, counter-clockwise from the positive x-axis.

**K.** For the square `(0,0), (4,0), (4,4), (0,4)`, classify the points `(4,2)`, `(2,2)` and `(5,2)` (0 outside, 1 boundary, 2 inside).

### 15.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Max Points on a Line | 149 | LeetCode | From each anchor, count equal reduced directions; do not divide |
| K Closest Points to Origin | 973 | LeetCode | Squared distance with `nth_element` or a size-`k` max-heap |
| Rectangle Overlap | 836 | LeetCode | Open-interval overlap on both axes (touching does not count) |
| Rectangle Area | 223 | LeetCode | Both areas minus the overlap area (zero if there is none) |
| Check If It Is a Straight Line | 1232 | LeetCode | All points collinear with the first two: `cross == 0` |
| Valid Boomerang | 1037 | LeetCode | Three points, distinct and not collinear: `orient != 0` |
| Largest Triangle Area | 812 | LeetCode | Try all triples with `area2`, or use the hull; divide by two at the end |
| Valid Square | 593 | LeetCode | Sort the six squared distances: four equal and two equal-larger, none zero |
| Erect the Fence | 587 | LeetCode | Convex hull that keeps collinear points on the boundary (`< 0`); watch duplicates |
| Minimum Area Rectangle | 939 | LeetCode | Put points in a set; for each diagonal pair check the other two corners |
| Random Point in Non-overlapping Rectangles | 497 | LeetCode | Weight by lattice point count (prefix sums, [11.03](03_combinatorics_probability_and_sampling.md)), then a uniform point inside |

### 15.3 In your editor

**A.** Compute the intersection point of two crossing segments as an exact fraction (numerator and denominator in 128 bits) and print it reduced.

**B.** Convex hull diameter: with the hull in hand, find the farthest pair by checking all hull pairs, then by rotating calipers in `O(h)`.

**C.** Point in a convex polygon in `O(log n)`: binary search on the angle from the first vertex, using `orient`.

**D.** Closest pair with a randomised grid: insert points in random order into a hash grid whose cell size is the current best distance, and rebuild the grid when the best improves. Compare with the divide and conquer.

**E.** Half-plane check for a polygon: write `is_convex(poly)` using the signs of consecutive `orient`, handling collinear vertices and both orientations.

### Answers

The program that checks the paper problems printed:

```
A 1 13
B 1 0
C 66 33.0
D 24 12 7
E 3 (0,0) (4,0) (2,4)
F 0 28
G 4
H (0,-1) (1,0)
I 1
J (1,0) (1,1) (0,1) (-1,0) (0,-1)
K 1 2 0
```

Reading it line by line:

**A.** The cross product is `5*3 - 1*2 = 13`, positive, so `orient = 1`: a left turn.

**B.** Yes (they overlap on the collinear stretch from 3 to 5), then no (collinear but disjoint).

**C.** `area2 = 66`, so the area is `33`: the rectangle `6 x 4 = 24` plus the roof triangle `6 * 3 / 2 = 9`.

**D.** `area2 = 24`. The edges have gcds 6, 2 and 4, so `B = 12`, and `I = (24 - 12 + 2) / 2 = 7`.

**E.** The three vertices `(0,0), (4,0), (2,4)`; `(2,1)`, `(2,2)` and `(1,1)` are inside the triangle.

**F.** No (they touch along a vertical edge piece of zero width), then `16 + 16 - 4 = 28`.

**G.** `4`: the line `y = 0` holds `(0,0), (1,0), (2,0), (3,0)`; the line `y = x` holds three.

**H.** `(1,0)` and `(0,-1)`, both at squared distance 1.

**I.** `1`, between `(10,0)` and `(10,1)`.

**J.** `(1,0), (1,1), (0,1), (-1,0), (0,-1)`.

**K.** `1` for `(4,2)` (on the right edge), `2` for `(2,2)` and `0` for `(5,2)`.

## Cheat sheet

- **Cross:** `a.x*b.y - a.y*b.x`, in 128 bits. `orient(a, b, c) = sign(cross(b - a, c - a))`: +1 left, 0 collinear, -1 right.
- **Never:** `double` slopes, `atan2`, square roots, or division in a geometry test. Compare squared distances.
- **Segments:** proper crossing (opposite signs both ways) or an endpoint on the other segment (collinear and in the bounding box).
- **Area:** shoelace `sum cross(p_i, p_(i+1))`, keep it doubled; positive means counter-clockwise. **Pick:** `2A = 2I + B - 2`, `B = sum gcd(|dx|, |dy|)`.
- **Point in polygon:** ray right, half-open rule `(a.y > q.y) != (b.y > q.y)`, boundary check first.
- **Hull:** sort, build lower and upper chains, pop while `orient <= 0` (`< 0` keeps collinear points); `O(n log n)`.
- **Rectangles:** `max(lefts) < min(rights)` on each axis; union = sum minus overlap.
- **Same line:** reduce `(dx, dy)` by the gcd and fix the sign. **Angle sort:** half-plane, then `cross > 0`.
- **`k` closest:** `nth_element` average `O(n)`; heap `O(n log k)`. **Closest pair:** divide and conquer with a strip, `O(n log n)`.

---

## Sources

- All code is tested here (3,231,272 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- The cross product, the shoelace formula, ray casting, Pick's theorem, Andrew's monotone chain (A. M. Andrew, 1979) and the divide-and-conquer closest pair are standard; see CLRS (the chapter on computational geometry) and de Berg, Cheong, van Kreveld and Overmars, *Computational Geometry: Algorithms and Applications*. The strip bound of about 7 neighbours is the classical packing argument, from memory. Cassini's identity is a standard Fibonacci identity. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
