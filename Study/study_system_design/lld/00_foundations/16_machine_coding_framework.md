# 16 — The machine coding framework

> Prerequisite: `03`-`09`. This is the repeatable procedure to run in *any*
> LLD interview, regardless of which problem you're given. Everything in
> `01_machine_coding_problems/` is this procedure applied to a specific
> prompt — read this file before your first problem, then re-read it after
> your first few attempts once you have real experience to check it against.

## First: which of the five LLD interview types are you actually in?

The underlying knowledge (OOP, SOLID, patterns, concurrency reasoning) is
identical across all five — but *what you're graded on producing* differs
enough that misreading the format costs real points. Identify which one
you're in in the first minute, from how the interviewer frames the
prompt, and adapt:

1. **Object-Oriented Design (OOD)** — whiteboard, a shared doc, or a
   virtual whiteboard tool; **pseudocode or class skeletons, not
   compiling code**. Graded on class/relationship identification and
   verbal reasoning. This program's Steps 1-3 below (clarify, entities,
   class diagram) *are* this format — if you're in a pure OOD round, you
   can often stop around Step 4 and spend the saved time going deeper on
   trade-off discussion instead of implementation.
2. **Machine Coding** — 60-90 minutes, a real IDE, fully compiling and
   running code expected. This is the format `01_machine_coding_problems/`
   is built for, and everything below in this file targets it directly.
   **Most common at India/APAC offices and startups**; increasingly common
   at US Big Tech too, especially for infra/platform roles — never assume
   you're safe from it.
3. **Concurrency Design** — whiteboard plus code, specifically probing
   thread-safety: race conditions, synchronization primitive choice,
   deadlock avoidance. `00_foundations/10_concurrency_for_lld.md` and
   `11_threads_futures_and_concurrency_patterns.md` are built directly for
   this format — if a prompt leads with "multiple threads will call this
   concurrently," you're here, and the concurrency reasoning *is* the
   interview, not a side extension to bolt on at minute 40.
4. **API Design** — 30-45 minutes, **no implementation at all**, just
   method signatures, naming, and interface shape for a library or
   service other engineers would consume. A genuinely distinct skill from
   the other four — covered in its own file,
   `00_foundations/20_api_design_interviews.md` (read it before an
   interview explicitly framed as "design the API for X," rather than
   "design X").
5. **Schema Design** — ER diagrams, normalization, indexing for a
   relational data model. Read `00_foundations/21_schema_design_interviews.md`
   (read it before an interview explicitly framed as "design the schema
   for X" — tables and keys, not classes). The deep, at-scale version of
   this skill (sharding, replication, storage-engine tradeoffs) still
   belongs to the HLD track; the LLD-scoped round — entities, cardinality,
   normalization to 3NF, indexing a specific query pattern — is covered in
   full there.

## The shape of a Machine Coding round specifically

Typical formats you'll see, know which one you're in within the first
minute:

- **45 minutes, single problem**: most common. Budget roughly 5 min clarify,
  10 min design (class diagram), 25 min implement, 5 min test/extend.
- **60-90 minutes, single problem with live extensions**: same opening, but
  expect the interviewer to add a requirement (concurrency, a new entity
  type) around the 30-40 minute mark. Leave slack in your design for this —
  it is not a trick, it's the actual test (see "extensibility" below).
- **Take-home + follow-up discussion**: no time pressure on the code itself,
  but the follow-up discussion will probe every design decision harder than
  a live round would. Write real tests; they will be read.

**The non-negotiable bar for this format, stated bluntly because it's the
single most common way candidates fail it**: your code must actually
**compile and run** by the end. A beautiful class diagram attached to
code that doesn't build is a fail, regardless of how sound the design
reasoning was — this is *why* this file's Step 5b below (the `main()`
driver proving the system works end to end) is not optional polish, it's
the actual deliverable this format is graded on.

## Step 1 — Clarify (3-5 minutes, do not skip)

The single most common way to lose points before writing a line of code:
starting to design against assumptions the interviewer didn't state and
wouldn't have chosen. Ask, out loud, in this order:

1. **Scope**: what's explicitly in and out? ("Should I handle payment, or
   just the parking/ticketing logic?")
2. **Actors and use cases**: who uses this system, and what are the 3-5 core
   actions? Write them down as a literal list before touching classes.
3. **Non-functional hints**: concurrency ("can two cars arrive at the same
   slot simultaneously?"), scale ("is this a single process or does it need
   to survive a restart?"), persistence (in-memory is almost always fine
   unless stated otherwise — say that assumption out loud).
4. **What's explicitly deferred**: state what you're *not* handling and why
   ("I'll assume single-currency pricing and revisit if there's time") —
   this converts a potential gap into a demonstrated scoping decision.

This step is also where you calibrate the interviewer's expectations of
depth — a Staff-level interviewer will often answer "however you think is
reasonable, that's part of what I'm evaluating" to a clarifying question,
which is itself information: you're expected to decide and defend it.

## Step 2 — Identify entities and responsibilities (5-8 minutes)

1. From the use-case list, extract **nouns** → candidate classes. Cross out
   nouns that are really just attributes of another noun.
2. From the use-case list, extract **verbs** → candidate methods, assign
   each to the class that most naturally owns the data it touches (a method
   belongs where the state it primarily reads/writes lives).
3. For each entity, ask: **is this a fixed, closed set of variants, or an
   open, growing one?** (Vehicle types: probably growing → interface +
   subclasses. Directions in an elevator: fixed at 2 → plain enum, no
   pattern needed.) This single question is what separates appropriate
   pattern use from pattern-itis (`00_how_to_study_this_track.md` warns
   about this directly).
4. Name the **one or two extension points** explicitly — say out loud which
   interface will absorb the change you expect to be asked for live.

## Step 3 — Draw the class diagram (do this before code, always)

Use the notation from `04_uml_and_modeling.md`. In a live round this can be
informal (boxes and arrows in a shared doc or on a literal whiteboard) — the
value is in forcing yourself to commit to relationships *before* they're
tangled in code, and in giving the interviewer a checkpoint to redirect you
early if you've misunderstood scope. **Narrate while you draw** — this is
where a large fraction of your "communication" score comes from, and it's
free: you're thinking out loud about something you'd be thinking anyway.

## Step 4 — Define interfaces before concrete classes

Write the abstract interfaces / pure virtual base classes first — this
forces you to nail down the contract (SOLID's `I` and `D`) before you're
deep in an implementation and tempted to leak details through it.

```cpp
// Write this first...
class PricingStrategy {
public:
    virtual double calculate(const Ticket&) const = 0;
    virtual ~PricingStrategy() = default;
};
// ...before this
class HourlyPricing : public PricingStrategy { /* ... */ };
```

## Step 5 — Implement the core path first

Get the **primary happy-path use case** working end to end before touching
edge cases or secondary features. A parking lot that can park and unpark one
car correctly is a stronger position at minute 30 than a half-finished
system attempting five features at once. Depth-first on the core, not
breadth-first across everything.

Inside this step, standard hygiene that's graded even under time pressure:

- Meaningful names, not `x`, `tmp`, `data2`.
- `const`-correctness and pass-by-reference where it's free — it's a signal
  you write production C++ daily, which you do.
- RAII for any resource (this you already know cold from
  `lld/notes/01_raii_and_modern_oop.md` — bring it forward here too).
- Don't silently swallow error cases — at minimum a comment or a thrown
  exception marking "this needs handling," which is honest and better than
  pretending it's not there.

## Step 5b — Write a `main()` driver that actually proves the system works

Distinct from Step 7's unit tests, and don't skip it even if you write
tests: instantiate the real dependency graph (concrete repository, concrete
strategies, injected into the concrete service — the Controller layer from
`14_application_architecture_patterns.md`, made real), and drive it through
a realistic sequence of calls end to end, printing or asserting the
results. This is the fastest way to catch an integration mistake a set of
narrow unit tests can miss (a wiring bug, a wrong constructor argument
order), and it's also literally the deliverable in a format many
interviewers explicitly grade — a `main()` that runs cleanly and prints
sane output is concrete, undeniable proof the design works, in a way a
class diagram alone never is.

```cpp
int main() {
    // Wire up the real dependency graph — this is where DIP (02) pays off:
    // every dependency was injected, so this is the one place concrete
    // types are chosen.
    auto pricing = std::make_unique<HourlyPricing>();
    ParkingLot lot(buildLevels(), std::move(pricing));

    // Simulate a realistic user flow, not just one call.
    Car car("ABC-123");
    auto ticket = lot.parkVehicle(car);
    if (!ticket) { std::cerr << "lot full\n"; return 1; }
    std::cout << "parked, ticket id=" << ticket->id << "\n";

    double cost = lot.unparkVehicle(*ticket);
    std::cout << "charged: " << cost << "\n";
}
```

## Step 6 — Handle edge cases, then extend

Once the happy path works, go back to your Step 1 notes and handle the
cases you deferred: capacity full, invalid input, concurrent access if
flagged. Then, if the interviewer introduces a live change, route it through
the extension point you named in Step 2 — this is the moment the whole
exercise was building toward, and naming it in advance ("I made pricing a
Strategy specifically so this kind of change is a new class, not an edit")
is the single highest-value sentence you can say in the entire round.

## Step 7 — Talk about testing and concurrency if there's time

- **Testing**: a couple of unit tests for the core class, especially any
  edge case you handled explicitly. If DIP (`05`) was applied correctly,
  this should be easy — that's the payoff of the earlier design decision,
  and saying so closes the loop nicely.
- **Concurrency**: if multiple actors could touch shared state concurrently
  (two cars for the last slot, two elevator requests), name where you'd add
  a lock (or a lock-free structure, cross-reference
  `lld/notes/03_thread_safe_primitives.md` and `04_lockfree_data_structures.md`
  if the interviewer wants depth) and why that scope, not a bigger one — a
  lock around the whole `ParkingLot` is a correct-but-blunt first answer; a
  lock scoped to the `Level` being searched for a free slot is the Staff
  follow-up if asked "how would you make this scale to many levels?"

## Time-boxing cheat sheet (45-minute round)

An equally valid way to carve the same 45 minutes, phrased around
deliverables rather than activities — use whichever framing sticks better,
they map onto the same seven steps above:

```
  0-10   Requirements: actors, use cases, scope boundary (Steps 1-2)
  10-20  Interfaces: abstract classes, structs, enums, DTOs — the ".h" layer,
         written before any implementation (Steps 3-4)
  20-35  Core logic: the ".cpp" layer, smart pointers throughout,
         happy path first (Steps 5-6)
  35-45  main() driver: wire real dependencies, simulate a real user flow
         end to end (Step 5b), plus tests/concurrency if time remains (Step 7)
```

The version below is the same allocation with slightly different phase
boundaries — both are fine; consistency in using *one* of them under
pressure matters more than which one:

```
  0-5    clarify
  5-13   entities, class diagram, name extension points
  13-35  implement core path
  35-42  edge cases + one live extension
  42-45  tests / concurrency discussion, wrap up
```

If you're at minute 20 and still drawing boxes, cut scope out loud ("I'll
implement Car and Motorcycle, and note Truck would follow the same
interface") rather than silently running out of time on the diagram.

## The rubric an interviewer is actually using

Roughly, and consistently across the companies this program targets:

1. **Requirements coverage** — did you build what was actually asked, not a
   different, more familiar problem?
2. **OOD correctness** — are responsibilities sensibly divided, are
   relationships (composition/aggregation/inheritance) modelled correctly?
3. **SOLID adherence in practice**, not recited — does the design survive
   the live extension without a rewrite?
4. **Code quality** — does it compile, does it actually run, is it
   readable, is it idiomatic modern C++?
5. **Communication** — did you narrate decisions, ask good clarifying
   questions, handle a changed requirement gracefully rather than defensively?

Notice code correctness is one of five axes, not the whole grade. Candidates
who go silent and just type tend to score worse than the code alone would
suggest, because axis 5 is worth real points and is graded continuously
through the whole session.

## Common mistakes, ranked by how often they cost real points

1. **Skipping clarification and solving the wrong-scoped problem.**
2. **Reaching for a pattern because you just learned it, not because the
   problem needs it** — an interviewer who's seen a thousand `ParkingLot`
   solutions notices a gratuitous `Visitor` immediately.
3. **A `switch`/`if`-chain on a type tag where the set is clearly open-ended**
   (the OCP violation from `05`, live in your own code).
4. **Ignoring a stated concurrency requirement** — if the prompt says "two
   attendants operate the gate simultaneously" and your design has no
   locking discussion, that's a direct miss, not a stylistic gap.
5. **Going silent.** Narrate. It's free points and it's also just a better
   way to think.

## Exercises

1. Time yourself running Steps 1-3 (clarify through class diagram, no
   code yet) on a problem you haven't seen from
   `01_machine_coding_problems/00_problem_list_and_roadmap.md` — target
   under 13 minutes including narrating out loud to an empty room.
2. Take a finished solution from the problem set, and write down which
   extension point you'd name if asked "what would you add next" — then
   check it against the "Extensions" section of that problem's file.
3. Practice the interrupt: have someone (or yourself, on a second pass) add
   a requirement to a design you've already implemented, and time how long
   it takes to route the change through your existing extension point vs.
   how long a rewrite would take. This is the concrete, measurable payoff of
   doing Steps 2-4 properly.
