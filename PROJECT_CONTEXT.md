# Study Programme: Context for Claude

This file is the briefing for any Claude chat attached to the **Study-Plan** project. The tutorials themselves are **not** stored in the project (it has a size cap). They live in GitHub and on Prince's Mac; this project holds only the plans, progress and rules.

## Who and what

Prince is a Senior Software Engineer at Ruckus (Wi-Fi platforms), strong in C/C++, low-level networking and embedded. He is preparing for Senior/Staff SWE and Embedded/Systems Infrastructure interviews (Google, Nvidia, Meta, Microsoft, Cisco, Palo Alto Networks) through a self-study programme. Treat him as a seasoned platform engineer: no superficial summaries.

## Where the material lives

| Place | Role |
|---|---|
| GitHub `https://github.com/Princeahrwar/Study.git`, branch `main` | The source of truth. Every tutorial and plan is committed here. The repository root is the `Study/` folder. |
| Prince's Mac, `Desktop/My Projects/Study` | A synced working copy. |
| This claude.ai project | Plans, progress, rules and this briefing only. Do not try to store tutorials here. |

To read a tutorial, fetch it from GitHub (or ask Prince to attach it); do not expect it in the project.

## How the tree is organised

`Study/` has numbered areas: 00_MASTER_PLAN.md, 01_dsa, 02_c_language, 03_cpp, 04_computer_organization, 05_operating_systems, 06_linux, 07_networking, 08_kernel, 09_drivers, 10_lld, 11_hld, 12_gpu_cuda, 13_embedded, 14_projects, 15_interviews, 99_toolchain. Each area has a `00_plan.md` with its tutorial list, problem list and progress table. `00_MASTER_PLAN.md` is the overview and `README.md` holds the status line and the next topic.

## The working method ("continue next")

When Prince says **"continue next"**, write the next unwritten tutorial in the plan (the README names it), then deliver it. The rules for every tutorial:

1. **Detailed, organised, systematic.** Basics to advanced, examples inside the text, right after the idea they explain.
2. **Few problems per tutorial**, each saying where to solve it (paper, LeetCode, editor) and how to approach it, with answers at the bottom. Each tutorial also has questions to answer out loud, a cheat sheet and a Sources section.
3. **Code is embedded in the tutorial.** No companion demo file unless the topic is special and important. No large harnesses, simulators or tooling inside tutorials. No labs folders unless asked.
4. **Every claim is verified by running code.** Compile with `-Wall -Wextra`, test against a brute force under AddressSanitizer and UBSan, print real outputs into the text, and run a verify script that checks the document (placeholders, style, code equals tested source, traces match a fresh run). Report the number of checks that passed.
5. **Be honest.** Say what was measured on this machine (cloud container, GCC 13.3, x86-64) and what is from memory. LeetCode numbers are from memory. Flag overshoots of length. No difficulty labels unless verified.
6. **Style:** no em dashes, no tabs, no trailing spaces, no words like "easy" or "trivial".
7. **Final reply format:** what was written (size), the checks, key findings and caveats, the sync status, the next topic.

## Delivery checklist for each tutorial

1. Write it in the container tree, then copy it to the GitHub clone, commit and push to `main`. Commit messages end with the attribution lines the session gives.
2. Sync to the Mac: send a tgz of the changed files, commit it to `Desktop/My Projects`, extract it, and compare the md5 of the new file on both sides.
3. Update the area plan, the track plan, the master plan and the README (status and next topic).
4. The claude.ai project needs no copy of the tutorial. Only refresh the plan files there when progress changes.

## Progress snapshot (2026-11-09)

- **DSA (area 01):** 104 of 104 tutorials written. Areas 01 to 11 are complete (arrays and strings, hashing, binary search and sorting, linked lists, stacks and queues, recursion and backtracking, trees, graphs, greedy and intervals, dynamic programming, bits and math). Area 12 (string algorithms) has 01 KMP and the Z-function, 02 rolling hash and Rabin-Karp, 03 palindromes and Manacher, 04 multi-pattern matching with Aho-Corasick, 05 automata and regular expressions and 06 suffix arrays and automata written (area complete). Area 13 (design-style coding) has 01 cache designs, 02 iterators and streams, 03 time-based structures and 04 in-memory systems written (area complete). Area 14 (theory and advanced techniques) has 01 P, NP and hard problems 02 randomised and approximate algorithms 03 sqrt techniques, offline queries and meet in the middle and 04 persistent and rollback structures written (area complete). Area 15 (pattern map and drills) has 01 recognising the pattern written. All 104 DSA tutorials are written; practice and the C track remain.
- **C language (area 02):** 8 tutorials planned; 01 types, conversions and undefined behaviour and 02 pointers and arrays are written. Next is 03 memory: stack, heap and malloc.
- **Other areas** (C++, computer organization, OS, Linux, networking, kernel, drivers, LLD, HLD, GPU, embedded, projects, interviews) have plans and some earlier material; see each `00_plan.md`.
- **Order:** finish the remaining DSA tutorials first (Prince asked for that), then continue the C track.

## Things not to do

- Do not delete tutorials from GitHub or the Mac.
- Do not create labs or demo files without asking.
- Do not store tutorials in this project.
