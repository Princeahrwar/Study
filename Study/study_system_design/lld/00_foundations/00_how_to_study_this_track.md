# How to study the LLD track

## Where this fits

**If C++ syntax itself — templates, smart pointers, lambdas, the STL,
exceptions — doesn't yet feel comfortable, start at
`cpp_primer/00_how_to_use_this_primer.md`, before anything below.** It
teaches the language mechanics every other file here assumes. If you
already read and write modern C++ comfortably, skip straight to Phase A.

The `lld/` folder then has three phases. Do them in order — each one
assumes the last.

```
  lld/00_foundations/cpp_primer/  Phase 0 (optional) — C++ language mechanics:
                                  templates, smart pointers, lambdas, STL,
                                  exceptions. Read this first if C++ syntax
                                  itself isn't yet comfortable.
  lld/00_foundations/            Phase A — OOD theory: OOP, SOLID, UML, patterns
  lld/01_machine_coding_problems/ Phase B — the interview problem set, worked in full
  lld/notes/                     Phase C — systems LLD: modern C++, concurrency,
                                  lock-free structures, HAL design (already written,
                                  Staff-level, assumes A and B — and note,
                                  `notes/06_lld_interview_problems.md` predates
                                  this whole program: 4 of its 6 problems are
                                  now superseded by longer, better versions in
                                  Phase B; only skim it, its Connection Pool
                                  section is the one genuinely new thing there)
```

If you only have time for one phase before an interview, do B — it's the one
that's actually rehearsed in a machine-coding round. But B will feel like
memorised recipes without A underneath it, and the interviewer's follow-up
questions ("why not just use inheritance here?") are exactly where A pays off.

## Why LLD is tested at all

A machine-coding round is not really testing whether you can write a
`ParkingLot` class. It's testing four things, in this order of importance:

1. **Can you turn a fuzzy English requirement into a set of classes,
   responsibilities and relationships?** This is the actual skill. Everything
   else is in service of it.
2. **Do you know the vocabulary?** SOLID, the GoF patterns, composition vs
   inheritance — not to recite definitions, but to use them as compressed
   arguments for a design decision. "I made this a Strategy so we can add
   pricing tiers without touching the ticketing logic" is a five-second
   sentence that would take thirty seconds to justify from scratch.
3. **Can you write it in working code in real time**, including getting the
   syntax right, under time pressure, without an IDE doing the thinking for
   you.
4. **Can you extend it live** when the interviewer changes the requirements
   halfway through — this is where a bad design (tight coupling, no
   extension points) collapses and a good one absorbs the change in two
   method additions.

Senior-level bar: you are expected to get to a working, reasonably clean
design *and* handle at least one live requirement change *and* talk about
concurrency or scale if asked, inside 45-60 minutes, without being walked
through it.

## How to actually study each topic

Same three rules as the rest of this program (see `00_roadmap/HOW_TO_USE.md`),
applied specifically to LLD:

1. **Draw the UML from memory.** After reading a pattern or a problem, close
   the file and redraw its class diagram on paper or in a blank Mermaid block.
   If you can't, you don't know it yet — you recognised it while reading,
   which is a much weaker form of knowledge.
2. **Implement without looking**, then diff against the reference solution.
   The diff is the lesson, not the reference code itself.
3. **Say the tradeoff out loud.** Every pattern has a cost (usually: more
   classes, one more level of indirection). Practice stating *why* that cost
   is worth paying for this specific problem — "unbounded conditionals in the
   pricing method" is a better answer than "it's a best practice."

## Study order inside `00_foundations/` — nine phases, beginner to advanced

The numbering below **is** the reading order, organised into the same nine
phases this program's full curriculum uses (`00_roadmap/CURRICULUM.md` has
the phase-by-phase rationale). Files are grouped visually by phase, but the
plain file numbers (`01`-`19`) are what every cross-reference in this track
uses — always read sequentially by number within `00_foundations/`.

```
  PHASE 1 — Modern C++ core mechanics (beginner)
  01  RAII, ownership & move semantics   RAII, unique_ptr/shared_ptr/weak_ptr,
                                          std::move, Rule of Zero/Three/Five —
                                          the mechanics that dictate every
                                          architectural choice that follows
  02  STL for system design              containers by complexity/iterator-
                                          stability, <algorithm>, std::function
                                          for lightweight Strategy

  PHASE 2 — Object-oriented analysis & UML (beginner)
  03  OOP fundamentals                   the four pillars, in C++, with the
                                          vtable mechanics underneath (you
                                          already have the hardware model for
                                          this from os_and_arch/01)
  04  UML and modelling                  class, sequence, use case and state
                                          diagram notation to whiteboard fast

  PHASE 3 — Structural principles & C++ idioms (intermediate)
  05  SOLID principles                   the five rules, each with a violation
                                          and a fix
  06  C++ design idioms                  Pimpl, CRTP, Type Erasure, NVI — how
                                          patterns actually get built in
                                          idiomatic modern C++

  PHASE 4 — Design patterns in modern C++ (intermediate)
  07  Creational patterns                Singleton (Meyers'), Factory Method,
                                          Abstract Factory, Builder, Prototype,
                                          Object Pool
  08  Structural patterns                Adapter, Bridge, Composite, Decorator,
                                          Facade, Flyweight, Proxy
  09  Behavioural patterns               Strategy, Observer, Command, State,
                                          Template Method, Chain of
                                          Responsibility, Iterator, Mediator,
                                          Memento, Visitor, Interpreter — with
                                          modern alternatives (std::function,
                                          weak_ptr, std::variant/std::visit)

  PHASE 5 — Concurrency, threading & resiliency (advanced)
  10  Concurrency for LLD                mutex/atomic/condition_variable,
                                          thread-safety extensions, the classic
                                          transfer() deadlock
  11  Threads & futures                  std::thread/jthread, async/future/
                                          promise, Dining Philosophers, when to
                                          reach for a pool
  12  Lock-free & thread-safe containers atomics/CAS/memory-order fundamentals,
                                          a thread-safe Stack, a fine-grained
                                          (striped-lock) HashMap

  PHASE 6 — Specialized data structures & algorithms (advanced)
  13  Specialized data structures        Trie, interval/calendar structures,
                                          graph + topological sort, LFU cache

  PHASE 7 — Application architecture & framework design (advanced)
  14  Application architecture           Controller/Service/Repository, an
                                          in-memory event bus, exception
                                          hierarchies, the Result pattern

  Cross-cutting (Phase 9 content, plus this track's own additions)
  15  Testing                            gtest, gmock, what makes a design
                                          testable, stress tests + ThreadSanitizer
  16  Machine coding framework           the repeatable procedure to run in any
                                          round, including the 45-minute plan
  17  Beyond SOLID                       DRY, KISS, YAGNI, Law of Demeter, and
                                          the anti-pattern catalog (God Object,
                                          Anemic Domain Model, Feature Envy)
  18  C++ OOP rapid fire                 quick-revision Q&A: virtual
                                          destructors, abstract class vs
                                          interface, Rule of Five, the
                                          constructor-virtual-call gotcha
  19  AI and LLD                         where AI tools genuinely help here,
                                          where they don't, and what
                                          interviewers now ask about it
  20  API design interviews              the 4th of 5 distinct LLD interview
                                          formats — signatures and contracts
                                          only, no implementation
  21  Schema design interviews           the 5th of 5 — entities, cardinality,
                                          normalization to 3NF, denormalization
                                          tradeoffs, and indexing, worked
                                          against the Splitwise/Library
                                          problems you already know
```

**Why `16`-`21` sit at the end rather than being interleaved**: `16`
(the machine-coding framework) deliberately comes *after* every theory
phase — running the execution procedure only makes sense once you have the
whole toolbox (OOP through architecture patterns) to apply within it. `17`
and `18` are reference/quick-revision material meant to be revisited
continuously rather than read once in sequence — treat them as flashcard
decks you return to, not a linear 17th and 18th chapter. `19` closes the
core track because it's explicitly cross-cutting commentary on everything
before it, not a prerequisite for anything. `20` and `21` are late
additions on purpose — API design and schema design as their own interview
formats only make sense once every pattern and principle they draw on
(ISP, DIP, Builder, the Result pattern for `20`; entities and cardinality
from the OOD process itself for `21`) is already familiar. `16` names all
five LLD interview formats explicitly (OOD/whiteboard, Machine Coding,
Concurrency Design, API Design, Schema Design) — `20` and `21` are the
dedicated treatments of the two that need one beyond the machine-coding
problem set itself.

For the deeper, Staff-level continuation of Phase 1's material specifically
(exact `noexcept`-driven optimisation behaviour, the full ownership-cost
table, `std::atomic_ref`) — `lld/notes/01_raii_and_modern_oop.md` and
`lld/notes/02_cpp_memory_model.md` in Phase C pick up from `01` at a deeper
level; read them once `01` is solid, not instead of it.

Then move to `01_machine_coding_problems/`, which applies all of the above to
a growing set of classic interview problems — `00_problem_list_and_roadmap.md`
tracks what's written, what's coming, and the recommended 10-problem solve
order.

## A warning about pattern-itis

The single most common way senior-but-not-yet-Staff candidates lose points in
LLD rounds is applying a pattern because they just learned it, not because the
problem needs it. A `Singleton` for a class that's instantiated once anyway
buys you nothing but a testability problem. A `Strategy` for a method that
will only ever have one implementation is speculative complexity. Read
`07`-`09` to build the vocabulary, but the actual skill in `16` and the
problem set is knowing when *not* to reach for one.
