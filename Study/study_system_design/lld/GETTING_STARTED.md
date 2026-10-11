# Getting started with the LLD track

One linear path through everything in this folder, from environment setup
to your first solved problem. Read this once, start to finish.

## 0. Mac setup — done and verified

Your toolchain is installed and confirmed working (not just installed —
actually tested: a real C++20 program was compiled with ASan+UBSan and run,
a real Mermaid diagram was rendered to SVG, and the full project below was
built and its tests executed):

| Tool | What it's for |
| --- | --- |
| Xcode Command Line Tools (Apple Clang 21) | The C++ compiler, full C++20 support |
| Homebrew | Package manager everything else came through |
| `cmake` + `ninja` | Builds the project in `include/`/`src/`/`tests/` below |
| `googletest` | Unit testing (fetched automatically by this project too — the brew copy is a convenience for standalone experiments) |
| `node` + `mermaid-cli` (`mmdc`) | Renders this track's Mermaid diagrams to standalone images |
| `pandoc`, LibreOffice (`soffice`), Poppler (`pdftoppm`) | The `.docx` conversion pipeline |

If you're setting up a **second machine** later, the commands that got you
here:

```bash
xcode-select --install   # click "Install" on the dialog; if it fails with
                          # a Software Update server error, install any
                          # pending macOS update first, then retry
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake ninja googletest node pandoc poppler
brew install --cask libreoffice
npm install -g @mermaid-js/mermaid-cli
```

An editor: **VS Code** (`brew install --cask visual-studio-code`) + the
"C/C++" and "CMake Tools" extensions is the easiest path if you don't
already have a preference; CLion or Vim+clangd both work fine too. This
project sets `CMAKE_EXPORT_COMPILE_COMMANDS ON`, so once you've configured
it once (Step 1 below), any editor that reads `compile_commands.json` gets
full autocomplete and go-to-definition for free.

## 1. The coding environment — build it once

From this directory:

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

This is a real, verified-working project:

- **`include/reference/`** — Tic-Tac-Toe and LRU Cache, fully implemented,
  faithful to their tutorials.
- **`tests/reference/`** — 10 gtest cases covering both, **all passing**,
  running under ASan+UBSan by default (any memory bug or undefined
  behavior in either implementation would fail the build, not just the
  test — that's the point of sanitizers-on-by-default in
  `CMakeLists.txt`).
- **`src/workspace/`** — skeletons for the other 8 problems in the
  **10-problem core sequence** (Parking Lot, Elevator, Splitwise, Vending
  Machine, Movie Ticket Booking, Message Queue/Pub-Sub, Rate Limiter, Task
  Scheduler). Every method compiles right now — each one throws
  `std::logic_error("TODO: ...")` as a placeholder. The whole project
  builds green with these stubs exactly as committed; replacing a `throw`
  with real logic is the exercise, not writing the class shape from
  scratch. The other 17 problems (full list in
  `01_machine_coding_problems/00_problem_list_and_roadmap.md`) don't have
  a wired-in stub yet — their tutorials are complete, so practice them by
  hand per section 2 below, or add a stub yourself following the pattern
  in an existing one if you want them wired into the build too.
- **`tests/workspace/`** — empty until you fill in a stub; its `README.md`
  explains exactly how to wire a new test in.

## 2. How to actually work a problem

**For the 8 problems with a workspace stub** (Parking Lot, Elevator,
Splitwise, Vending Machine, Movie Ticket Booking, Message Queue/Pub-Sub,
Rate Limiter, Task Scheduler):

1. Read that tutorial in `01_machine_coding_problems/` up through
   **Step 3 — Class diagram**, then stop.
2. Open the matching skeleton in `src/workspace/` (see that folder's
   `README.md`). Replace every `throw` with your own implementation, timed
   — 45 minutes, per `00_foundations/16_machine_coding_framework.md`.
3. Write a test in `tests/workspace/`, wire it into `tests/CMakeLists.txt`
   (an example block is commented in there), and get it green:
   ```bash
   cmake --build build && ctest --test-dir build --output-on-failure
   ```
4. **Only now** read the tutorial's full Step 4-5 code and diff your
   version against it. That diff is the actual lesson.

**For the remaining 19 problems** (Tic-Tac-Toe, LRU Cache, and the 17
without a wired-in stub) — same method, just without the CMake scaffolding
doing the file/build setup for you:

1. Read the tutorial up through Step 3, then stop.
2. Create your own `.cpp` file anywhere convenient (a scratch folder, or a
   new file under `src/workspace/` following an existing stub's shape if
   you want it in the build), and implement it cold, timed.
3. Compile and run it by hand:
   ```bash
   clang++ -std=c++20 -fsanitize=address,undefined -g -o solution solution.cpp && ./solution
   ```
4. Diff against the tutorial's Step 4-5 code.

Tic-Tac-Toe and LRU Cache specifically are already fully worked in
`include/reference/` — study them as the "what a finished one looks like"
reference before attempting an unfamiliar problem cold.

## 3. The reading order

**If modern C++ syntax — templates, smart pointers, lambdas, the STL,
exceptions — doesn't yet feel comfortable, start at
`00_foundations/cpp_primer/00_how_to_use_this_primer.md` before anything
else.** It's a from-scratch, beginner-friendly primer covering exactly the
C++ constructs every tutorial below uses without re-explaining — 13 short
files, meant to be skimmed once and returned to as reference. If C++
already feels comfortable, skip it and start at the file below.

```
  00_foundations/cpp_primer/ (optional)          <- C++ language mechanics,
       |                                            if not yet comfortable
       v
  00_foundations/00_how_to_study_this_track.md   <- read this first, always
       |
       v
  00_foundations/01 through 19                   <- theory, in file-number order
       |                                            (see that file for the full
       |                                            phase-by-phase breakdown)
       v
  01_machine_coding_problems/00_problem_list_and_roadmap.md
       |                                  <- read this first: the 10-problem
       v                                     core sequence, then the extended
       |                                     order for the remaining 17
  01_machine_coding_problems/ (all 27,       <- practice: core sequence first,
                              28 w/ Chess)      then the extended order
       |
       v
  notes/ (Phase C)                                <- Staff-depth systems LLD,
                                                      once 00_foundations + the
                                                      problem set are solid
```

`00_foundations/00_how_to_study_this_track.md` has the full rationale for
this order and the three study habits (draw from memory, implement without
looking, say the tradeoff out loud) that make it stick.

## 4. Content coverage — what was actually checked

Before writing this guide, every file in `00_foundations/`,
`01_machine_coding_problems/`, and `notes/` was reviewed for completeness:

- No file was corrupted or truncated during the phase-based renumbering —
  word counts match pre-renumbering totals for every file in `notes/`, and
  nothing in `00_foundations/` or `01_machine_coding_problems/` came back
  suspiciously short.
- Every cross-reference (both full filenames and shorthand ones like
  `` `00_foundations/09` ``) resolves to a file that actually exists at
  that number.
- All 20 `00_foundations/` files map cleanly onto the full 9-phase modern-
  C++-LLD syllabus this track follows, with no gaps.
- `01_machine_coding_problems/` covers **all 27 roadmap problems in full**
  (28 with optional Chess), across all four tiers including the Tier 4
  embedded/HAL-flavoured differentiators (Traffic Light Controller,
  Pluggable Sensor/HAL Framework, OTA Update Manager) — the roadmap in
  `00_problem_list_and_roadmap.md` is complete, not a partial list.

## 5. Reading on paper, sharing, or in Google Docs

Every tutorial also exists as a `.docx` in `docs/`, mirroring the same
three-folder structure, with all 26 diagrams rendered as embedded images
and every table and code block intact. Upload any of them to Google Drive
and it opens as an editable Google Doc. `docs/README.md` explains how they
were generated and how to regenerate one if you edit its source `.md` —
the Markdown remains the maintained master copy; the `.docx` files are a
generated convenience.

## 6. Rendering the diagrams

Every tutorial's Mermaid diagrams (class, sequence, use case, state) render
natively in VS Code (with the "Markdown Preview Mermaid Support"
extension), in GitHub, and in Claude Code's own file viewer — no setup
needed for reading. To export one as a standalone image:

```bash
mmdc -i path/to/file.md -o diagram.svg   # or -o diagram.png
```

The pre-existing `.mmd` diagram source files in `notes/diagrams/`,
`../hld/diagrams/`, `../kernel_drivers/notes/diagrams/`, and
`../os_and_arch/notes/diagrams/` (written before this track's diagrams
moved to inline Mermaid) use the same tool.

## 7. What "done" looks like for this track

You've made it through when you can, cold, in under 50 minutes with someone
watching:

- Take an unfamiliar prompt shaped like one of the 27 problems in
  `01_machine_coding_problems/00_problem_list_and_roadmap.md`
- Clarify scope, draw a class diagram, name your extension point — all in
  the first 10-13 minutes, out loud
- Implement the core path, handle the stated edge cases, and absorb one
  live requirement change without a rewrite
- Answer "how would you test this" and "how would you make this
  thread-safe" with a real, specific answer, not a shrug

Everything in `00_foundations/` and every worked problem exists to get you
to that bar.
