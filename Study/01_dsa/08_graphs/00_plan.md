# Graphs: Area Plan

A graph is the general case that trees, grids, state machines, dependency lists and networks are all special cases of. This area starts with how to store one and how to walk it (breadth-first and depth-first), then asks the standard questions in order: what is connected to what, in what order must things happen, what is the fewest steps, what is the cheapest route, what is the cheapest way to connect everything, which parts are strongly connected, and how much can flow through a network.

**Language:** C++ · **Time:** about 2 weeks · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, memory or operation counts is checked by running the code, on a machine named in the text, with two runs.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. A companion `*_demo.cpp` exists only where a tutorial starts a new kind of structure or feeds a project. In this area the code is inside the tutorials.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_representations_and_traversal.md`](01_representations_and_traversal.md) | Edge list, adjacency list, adjacency matrix and compressed rows (CSR), with measured memory and build cost; BFS with distances, parents and path reconstruction; depth-first search without recursion, with discovery and finish times and the four edge kinds; the "mark on push or on pop" trap with a counterexample and a measured stack size; grids as implicit graphs; the recursion depth limit on a path graph | Written |
| 02 | [`02_connectivity_and_cycles.md`](02_connectivity_and_cycles.md) | Connected components by search and by union-find, cycles in undirected graphs (skip the arrival edge, not the parent) and directed graphs (three colours), bipartite check with an odd-cycle proof, grid regions and flood fill; measured giant-component threshold, ring recursion crash, grid percolation | Written |
| 03 | `03_topological_sort.md` | Kahn's algorithm and DFS order, course schedule, alien dictionary, build systems | To write |
| 04 | `04_bfs_on_states_and_grids.md` | Multi-source BFS, word ladder, sliding puzzle and lock problems as state graphs, bitmask states, bidirectional BFS, 0-1 BFS, A* as guided search | To write |
| 05 | `05_shortest_paths_dijkstra.md` | Dijkstra with a heap, why negative edges break it, path reconstruction and counting paths, state-augmented Dijkstra, grid Dijkstra | To write |
| 06 | `06_shortest_paths_negative_edges_and_all_pairs.md` | Bellman-Ford and negative cycles, shortest paths in a DAG, Floyd-Warshall, transitive closure, Johnson's algorithm | To write |
| 07 | `07_minimum_spanning_trees.md` | Kruskal with union-find, Prim with a heap, the cut property, connecting points at minimum cost | To write |
| 08 | `08_advanced_graphs.md` | Strongly connected components (Tarjan, Kosaraju), bridges and articulation points, Euler paths, 2-SAT as an idea | To write |
| 09 | `09_network_flow_and_bipartite_matching.md` | Ford-Fulkerson and Edmonds-Karp, Dinic, max-flow min-cut, matching and Hall's theorem, assignment problems (stretch) | To write |

---

## Problem list (about 90 with practice)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them. The numbers and titles below are LeetCode's, **from memory**, and are checked again when each tutorial is written; a problem marked "premium" needs a paid LeetCode account.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Find if Path Exists in Graph (1971), Clone Graph (133), Number of Provinces (547), Find the Town Judge (997), Find Center of Star Graph (1791), All Paths From Source to Target (797), Keys and Rooms (841), Shortest Path in Binary Matrix (1091), Flood Fill (733), Number of Islands (200), Max Area of Island (695) |
| 02 | Course Schedule (207), Graph Valid Tree (261, premium), Number of Connected Components in an Undirected Graph (323, premium), Is Graph Bipartite? (785), Possible Bipartition (886), Find Eventual Safe States (802), Surrounded Regions (130), Pacific Atlantic Water Flow (417), Number of Enclaves (1020) |
| 03 | Course Schedule II (210), Alien Dictionary (269, premium), Sequence Reconstruction (444), Parallel Courses (1136), Find All Possible Recipes from Given Supplies (2115), Sort Items by Groups Respecting Dependencies (1203), Longest Increasing Path in a Matrix (329) |
| 04 | Rotting Oranges (994), 01 Matrix (542), Word Ladder (127), Word Ladder II (126), Open the Lock (752), Sliding Puzzle (773), Snakes and Ladders (909), Shortest Bridge (934), Shortest Path in a Grid with Obstacles Elimination (1293), Shortest Path to Get All Keys (864), Minimum Genetic Mutation (433), As Far from Land as Possible (1162) |
| 05 | Network Delay Time (743), Path With Minimum Effort (1631), Cheapest Flights Within K Stops (787), Path With Maximum Probability (1514), Swim in Rising Water (778), Number of Ways to Arrive at Destination (1976) |
| 06 | Find the City With the Smallest Number of Neighbors at a Threshold Distance (1334), Evaluate Division (399), Cheapest Flights Within K Stops (787, again with Bellman-Ford), Minimum Cost to Convert String I (2976); in your editor: detect a negative cycle |
| 07 | Min Cost to Connect All Points (1584), Connecting Cities With Minimum Cost (1135, premium), Find Critical and Pseudo-Critical Edges in Minimum Spanning Tree (1489), Optimize Water Distribution in a Village (1168, premium) |
| 08 | Critical Connections in a Network (1192), Reconstruct Itinerary (332), Valid Arrangement of Pairs (2097), Minimum Number of Days to Disconnect Island (1568), Maximum Employees to Be Invited to a Meeting (2127) |
| 09 | Maximum Students Taking Exam (1349), Maximum Number of Accepted Invitations (1820), Maximum Compatibility Score Sum (1947); in your editor: Dinic on a unit-capacity bipartite graph |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- CLRS chapters 20 to 26 (elementary graph algorithms, minimum spanning trees, single-source and all-pairs shortest paths, maximum flow).
- Sedgewick and Wayne, *Algorithms*, chapter 4 (graphs): clean implementations and the history of each algorithm.
- MIT 6.006 (Spring 2020) lectures on BFS, DFS, topological sort, Bellman-Ford and Dijkstra.
- NeetCode 150, section "Graphs" and "Advanced Graphs" (neetcode.io): the same problems with video walkthroughs.
- Striver's Graph Series (takeUforward): C++ notes and videos in the order of this area.
- The Algorithms for Competitive Programming site (cp-algorithms.com): proofs and variants for every algorithm in tutorials 05 to 09.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Representations and traversal | Written |
| 02 | Connectivity and cycles | Written |
| 03 | Topological sort | Next |
| 04 | BFS on states and grids | To write |
| 05 | Shortest paths I: Dijkstra | To write |
| 06 | Shortest paths II: negative edges and all pairs | To write |
| 07 | Minimum spanning trees | To write |
| 08 | Advanced graphs | To write |
| 09 | Network flow and bipartite matching | To write |

Back to the DSA track view: [`../00_plan.md`](../00_plan.md).
