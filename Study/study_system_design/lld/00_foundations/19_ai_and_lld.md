# 19 — AI and LLD: where it helps, where it doesn't, and what gets asked

> Prerequisite: `03`-`16`. This file is scoped to LLD specifically. A
> broader, program-wide view of AI's role across all six focus areas belongs
> in a future `ai_scope/` track referenced from the root `README.md` — this
> file is the LLD-specific slice of that larger picture.

## The honest split: interview vs. job

These are different questions and this program treats them differently.

**In the interview itself: assume AI tools are not allowed, unless told
otherwise.** Live LLD/machine-coding rounds at every company this program
targets (Google, Meta, Microsoft, Nvidia, Cisco, Palo Alto Networks) are
proctored or screen-shared specifically to prevent this, because the round
exists to measure *your* design reasoning under time pressure, which an LLM
would substantially do for you. A minority of take-home formats explicitly
permit AI assistance and say so — if that's not stated, ask your recruiter
directly rather than assuming either way; guessing wrong here is a
disqualifying integrity issue, not a minor deduction.

**On the job, AI-assisted design and coding is now normal, and some loops
test for it directly** — Microsoft and Google in particular have added
explicit "how do you use AI in your day-to-day engineering" conversations to
senior loops since 2024-2025. Having a real, specific answer (not "I use
Copilot for autocomplete") is worth preparing deliberately, covered below.

## Where AI genuinely accelerates LLD work, day to day

- **Scaffolding boilerplate once the design is fixed.** Once you've decided
  on a `Strategy` interface with three implementations, generating the
  repetitive class skeletons is exactly the kind of mechanical work a model
  does well and fast — the design decision was yours, the typing wasn't
  worth your time.
- **Design review against a checklist.** Pasting a class diagram or a header
  file and asking "does this violate any SOLID principle, and where would
  you add an extension point for X" is a genuinely useful second pair of
  eyes — treat the answer as a hypothesis to evaluate, not a verdict, the
  same way you'd treat a junior colleague's review comment.
- **Generating edge cases you didn't think of.** "What edge cases does this
  `ParkingLot::park()` implementation miss?" is a good use — verifying the
  suggestions is still your job, but the initial sweep is fast.
- **Explaining unfamiliar code that uses a pattern you don't recognise.**
  Legitimate and fast, especially for legacy code with no comments.
- **Generating variant practice problems.** "Give me a problem with the same
  shape as a vending machine but themed around a traffic light controller"
  is a good way to get more reps without exhausting a fixed problem list —
  this is a genuinely good use of AI *for studying this exact track*.

## Where it actively hurts your interview readiness if you lean on it

- **Skipping the struggle of deciding which pattern fits.** The actual
  skill tested in an LLD round is *recognising* "this is a Strategy
  situation" from a fuzzy English prompt — if a model always tells you the
  answer, you never build the pattern-matching instinct that has to fire in
  real time, unaided, in the room. Do the exercises in `07`-`09` yourself
  first; use AI to check afterward, not to solve.
- **Cargo-culting patterns AI suggests without understanding the tradeoff.**
  A model asked "how should I structure this" will frequently suggest a
  pattern reflexively (it's seen a lot of textbook examples), including in
  cases where the extra indirection isn't earning its cost. This is the
  exact "pattern-itis" failure mode flagged repeatedly in this track
  (`00_how_to_study_this_track.md`, `07`) — AI can make it worse, not
  better, if you don't independently evaluate whether the suggested pattern
  actually fits the problem's real shape (fixed set vs. growing set, one
  axis of variation vs. two).
- **Losing the rehearsal.** The "say it out loud, on a timer, to an empty
  room" habit from `00_roadmap/HOW_TO_USE.md` doesn't transfer if AI did
  the thinking — the interview is testing your live reasoning, and live
  reasoning is a trained skill with no shortcut.

## A real answer to "how do you use AI in your engineering workflow"

Interviewers asking this in 2025-2026 are checking for judgment, not
enthusiasm. A strong senior/staff-level answer has three parts, and you
should prepare your own honest version of each before the loop, not
improvise it live:

1. **A specific, concrete example** of a task where AI assistance measurably
   helped (scaffolding, a design review catch, an edge case you'd have
   missed) — vague answers ("I use it for everything") read as unprepared.
2. **A specific example of where you didn't trust it**, and why — this is
   the half most candidates skip, and it's the half that actually
   distinguishes judgment from enthusiasm. A good answer names a concrete
   failure mode: confidently wrong about a concurrency guarantee, suggesting
   a pattern that didn't fit, hallucinating an API that doesn't exist.
3. **How you verify AI-suggested code before it ships** — tests you still
   write yourself, review discipline you still apply, the fact that you
   remain accountable for what you submit regardless of its origin.

## Interview angles

**"Would you use AI-generated code in production without review?"** No —
you review and test it exactly as you would a colleague's PR, arguably more
carefully for anything touching concurrency or memory safety given known
failure modes in generated code around both.

**"What's a time AI gave you a wrong or misleading suggestion?"** Have a
real one ready. If you genuinely can't think of one, that's worth noticing
about your own usage — it likely means you haven't pushed it into
unfamiliar-enough territory to see it fail, which is itself worth saying
honestly rather than fabricating an example.

## Exercises

1. Take a finished solution from `01_machine_coding_problems/`, paste the
   class diagram into an AI assistant, and ask for a SOLID critique. Compare
   its critique against your own — where does it agree with what you
   already knew, and where does it surface something real?
2. Ask an AI assistant to suggest a design for one of the problems in
   `00_problem_list_and_roadmap.md` you haven't attempted yet. Before
   reading its answer, write your own design for 10 minutes. Then compare —
   the gap between the two, and specifically *why* you'd push back on any
   part of its suggestion, is the actual exercise.
3. Draft your own two-part answer to "how do you use AI in your engineering
   workflow" — one real win, one real place you didn't trust it. Say it out
   loud on a timer, the same way you'd rehearse a design answer.
