# 17 — Beyond SOLID: DRY, KISS, YAGNI, Law of Demeter, and the anti-pattern catalog

> Prerequisite: `03`-`05`. SOLID is the headline set, but interviewers
> reach for this second, smaller set of principles just as often —
> especially as the counter-argument to *over*-applying SOLID and the
> patterns from `07`-`09`. This file also gives you the vocabulary to
> critique code on sight, which shows up directly in "what's wrong with
> this class?" style questions.

## DRY — Don't Repeat Yourself

**Every piece of knowledge should have a single, unambiguous representation
in the system.** Not "never write similar-looking code twice" — *knowledge*
duplication, not *textual* duplication. Two functions that happen to look
alike but encode genuinely different business rules are not a DRY
violation, even if merging them would reduce line count.

```cpp
// Looks like duplication, isn't: two different business rules that happen
// to share a shape today. Merging them couples two things that change for
// different reasons — the moment one rule changes independently, you're
// back to an if-branch inside the "shared" function, which is worse.
double calculateEmployeeBonus(double salary) { return salary * 0.10; }
double calculateContractorBonus(double salary) { return salary * 0.10; }
```

**The rule of three**: don't abstract on the first duplication — a single
repeat is often coincidence. On the third occurrence of genuinely the same
*knowledge*, extract it. Abstracting too early on too little evidence
produces the wrong abstraction, which is more expensive to unwind than
duplication would have been.

## KISS — Keep It Simple, Stupid

**Prefer the simplest design that correctly solves the stated problem.**
This is the direct counterweight to pattern-itis, named explicitly as a
warning in `00_how_to_study_this_track.md` and `07_design_patterns_creational.md`'s
Singleton/Prototype sections. If a plain function does the job, a `Strategy`
hierarchy around it is a KISS violation even though it's not wrong SOLID.

**How this plays out live**: an interviewer who sees you reach for a
`Visitor` (`09`) on a two-class hierarchy with one operation is watching for
exactly this — KISS violated in service of showing off pattern knowledge.
The fix is a habit, not a rule: after proposing a pattern, ask yourself "is
there a plainer way that's just as correct?" before committing to it.

## YAGNI — You Aren't Gonna Need It

**Don't build extensibility for a requirement nobody has stated.** OCP
(`05_solid_principles.md`) says design for the *changes you actually
expect*; YAGNI is the discipline that keeps you from designing for changes
you're merely imagining. The two aren't in tension when applied correctly —
OCP without YAGNI produces speculative interfaces with one implementation
and no real second use case in sight.

**The concrete test**: before adding an interface "in case we need to swap
implementations later," name the *specific* second implementation you
expect. If you can't name one, you're speculating — build the concrete
class, and add the interface when (if) the second implementation actually
materializes. This exact tension is why `00_how_to_study_this_track.md`
warns against Singleton, and why `07`'s Prototype section says most
call sites don't need it.

## Law of Demeter (Principle of Least Knowledge)

**A method should only call methods on: itself, its parameters, objects it
creates, or its own direct component objects — never on an object obtained
*from* one of those.** Informally: "don't talk to strangers," or "only talk
to your immediate friends."

```cpp
// Violation: reaching through Car -> Engine -> FuelTank -> getLevel()
double level = car.getEngine().getFuelTank().getLevel();

// Fixed: Car exposes the operation directly, hiding its internal structure
double level = car.getFuelLevel();   // Car delegates internally
```

The violation form is sometimes called a **"train wreck"** (a chain of
dots). It's a coupling problem specifically: the caller now depends on
`Car`'s *internal composition* (that it has an `Engine` with a `FuelTank`),
not just `Car`'s interface — if `Car` is refactored to get fuel data a
different way internally, every caller with a train-wreck chain breaks.
This is Demeter's Law giving a concrete, checkable form to the "low
coupling" goal from `03_oop_fundamentals.md`.

**Caveat interviewers respect**: fluent builders (`07`'s `Builder` pattern)
chain calls on purpose, and that's fine — Demeter's Law is about reaching
into *different, unrelated* objects through a chain, not about a builder
returning `*this` repeatedly. Know the difference so you don't over-apply
this one and flag a legitimate fluent interface as a violation.

## Separation of Concerns — the umbrella term the others sit under

Worth naming explicitly, because it's the term interviewers sometimes
use as a catch-all instead of any single principle above: **each part of
a system should address one concern, with minimal overlap with other
parts.** SRP (`05_solid_principles.md`) is Separation of Concerns applied
to a single class. The Controller/Service/Repository layering
(`14_application_architecture_patterns.md`) is Separation of Concerns
applied to a whole application (parsing input, business logic, and
storage are three separate concerns, three separate layers). Low coupling
/ high cohesion (`03_oop_fundamentals.md`) is Separation of Concerns
stated as a design *quality* rather than a rule. If an interviewer says
"talk to me about separation of concerns in this design," they're asking
you to identify the distinct responsibilities in your solution and
confirm each lives in exactly one place — the same thing you're already
doing when you apply SRP, just asked about at a broader scope.

## The anti-pattern catalog — code smells to recognise on sight

These come up constantly in "what's wrong with this design?" questions,
where you're shown a class or two and asked to critique. Know the name for
each — naming it precisely is worth more than describing it vaguely.

| Anti-pattern | What it looks like | Which principle it violates |
| --- | --- | --- |
| **God Object** | One class doing authentication, business logic, persistence, and formatting | SRP (`05`) — low cohesion taken to the extreme |
| **Anemic Domain Model** | Classes are just data bags (all public fields, no behaviour); all logic lives in separate `*Service` classes | Encapsulation (`03`) — data and the behaviour that maintains its invariants have been pulled apart |
| **Spaghetti Code** | Control flow with no discernible structure — deeply nested conditionals, no clear ownership of any step | Cohesion generally; often a sign no design phase (Steps 2-3 of `16_machine_coding_framework.md`) happened at all |
| **Golden Hammer** | The same familiar pattern (often Singleton or a big `switch`) applied to every problem regardless of fit | KISS, and the pattern-itis warning throughout this track |
| **Feature Envy** | A method in class `A` uses class `B`'s data/methods far more than its own | SRP — the method's logic probably belongs on `B`, not `A` |
| **Shotgun Surgery** | One conceptual change requires editing many unrelated classes | OCP (`05`) — the opposite of a well-placed extension point |
| **Lava Flow** | Dead or obsolete code kept "just in case," with nobody sure if it's safe to remove | Accumulated technical debt; not a SOLID violation per se, but a real signal in legacy-code discussions |
| **Magic Numbers/Strings** | Unexplained literal values (`if (status == 3)`) scattered through logic | Not a SOLID violation, but a readability/maintainability smell worth naming — replace with named constants or an enum |
| **Train Wreck** | `a.getB().getC().doSomething()` chains | Law of Demeter, above |

## How these principles interact with SOLID — the honest tension

SOLID (especially OCP) pushes toward more interfaces and more extension
points. DRY, KISS, and YAGNI push back toward less structure until it's
earned. **Neither side is "more correct"** — the actual skill, and the
thing a Staff-level answer demonstrates, is judging *which side applies to
this specific requirement*. A payment-type field that's explicitly said to
be growing (the interviewer mentions "we'll add more payment methods next
quarter") — build the Strategy now, OCP wins. A two-value enum with no
signal it'll ever grow — don't, YAGNI wins. State the reasoning, not just
the conclusion.

## Interview angles

**"What's wrong with this class?"** (shown a God Object or Anemic Domain
Model) — name the anti-pattern precisely, then the specific fix (usually:
extract a class along a real axis of change, or move logic from a `Service`
onto the data class it operates on).

**"Isn't this over-engineered?"** — a genuinely fair challenge to a design
with more than one or two patterns in it. Have a real answer ready: which
specific stated requirement justified each pattern, not a general defence
of "best practices."

## Exercises

1. Find a train wreck (`a.getB().getC()...`) in code you've written or
   reviewed. Rewrite it as a delegating method on the outer class, and
   state in one sentence what coupling you removed.
2. Take the `Feature Envy` smell and construct a two-class example that
   exhibits it, then refactor by moving the method to the class whose data
   it actually uses.
3. Review one of your own solutions from
   `01_machine_coding_problems/` and find one place, honestly, where you
   applied a pattern that YAGNI would have argued against. Rewrite it
   simpler and note what you'd say if an interviewer challenged the
   original version as over-engineered.
