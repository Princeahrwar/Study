# lld/ — three phases, in order

**Start with `GETTING_STARTED.md`** — the full reading order and how the
buildable `include/`/`src/`/`tests/` project below is meant to be used. The
Mac toolchain (Xcode CLT, Homebrew, cmake, googletest, node/mermaid-cli,
pandoc/LibreOffice) is installed and verified working — a real C++20
program compiled and ran under ASan+UBSan, and the project below builds
clean with all 10 reference tests passing. This file is just the folder
map.

```
  00_foundations/cpp_primer/  Phase 0 (optional) — a from-scratch C++
                               language primer (templates, smart pointers,
                               lambdas, STL, exceptions). Start here if
                               modern C++ syntax itself isn't yet
                               comfortable — everything below assumes it.

  00_foundations/             Phase A — 22 files (00-21), sequenced through
                               nine sub-phases: modern C++ mechanics, OOAD/
                               UML, SOLID/idioms, design patterns,
                               concurrency, specialized data structures,
                               architecture, plus testing/framework/AI.

  01_machine_coding_problems/ Phase B — the interview problem set. Full
                               requirements, class diagrams, working C++,
                               and live-extension scenarios for each.

  notes/                      Phase C — systems LLD. Modern C++ ownership,
                               the memory model, lock-free structures, HAL
                               design, and a second interview problem set
                               (06_lld_interview_problems.md) at Staff depth.
                               Assumes A and B.

  include/, src/, tests/      A real, buildable CMake project — verified,
                               not just written. Reference implementations
                               (Tic-Tac-Toe, LRU Cache) with 10 passing
                               gtest cases running under sanitizers by
                               default, plus compiling skeleton stubs for
                               every other problem so "implement it
                               yourself" has somewhere to actually happen.

  docs/                       Every tutorial above (76 files, all three
                               phases) as a real .docx, diagrams rendered
                               as embedded images, tables and code kept
                               intact — upload to Google Drive and it opens
                               as an editable Google Doc. The .md files
                               here remain the maintained source; docs/ is
                               generated from them. See docs/README.md.
```

Read `00_foundations/00_how_to_study_this_track.md` for why the phases are
ordered this way and how to study each one so it actually sticks —
`GETTING_STARTED.md` is the "what do I run/open first" companion to it.
