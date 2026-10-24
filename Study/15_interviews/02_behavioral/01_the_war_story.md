# The war story at three zoom levels

## Why one story, not ten

Across six companies you will be asked some version of "tell me about a hard
technical problem" perhaps fifteen times. Preparing fifteen stories produces
fifteen shallow ones. Prepare **one** genuinely deep story you can tell at three
depths, plus two or three backups for specific themes (conflict, failure,
influence).

The depth you can go to is what signals seniority. A story you can only tell for
two minutes is a story you did not really own.

## Choosing it

The best candidate story has all of these:

- **You personally did the work.** Not "we". Interviewers probe for this and it
  is obvious when the answer is a team's.
- **It was genuinely hard**, in a way you can make legible in one sentence.
- **It has a number** — latency, throughput, incidents avoided, devices affected.
- **It crosses layers**, so it can be told to a hardware person or a distributed
  systems person.
- **Something went wrong first.** A story where you were right immediately is
  boring and slightly suspect.

Your material almost certainly includes a good one: a field-only bug, a race that
appeared at scale, a performance regression, a bad rollout.

## Level 1 — 90 seconds, behavioral

For "tell me about a challenging project." Structure, roughly STAR but weighted
toward the action:

```
  Situation   one sentence. What system, what scale, why it mattered.
  Task        one sentence. What was broken and what you owned.
  Action      four or five sentences. The METHOD: what you measured, what you
              ruled out, what the deciding evidence was.
  Result      one sentence WITH A NUMBER, plus what you changed so it could not
              recur silently.
```

That last clause is the Staff marker and most candidates omit it. Fixing a bug is
senior. Making the class of bug detectable is Staff.

## Level 2 — 10 minutes, with a whiteboard

For "walk me through it" in a design or deep-dive round. Now you draw:

- The system, with the relevant components only. Resist drawing everything.
- Where the failure manifested versus where it originated — usually different
  places, and the gap is the interesting part.
- The hypotheses you held, and **the measurement that discriminated between
  them.** This is the heart of the story.
- The fix, and the alternative fixes you rejected with reasons.

Rehearse the drawing. A story you can draw is more convincing than one you
narrate, and drawing buys you thinking time.

## Level 3 — 40 minutes, down to the cache line

For a deep-dive round with a senior engineer who wants to find the bottom of your
knowledge. Be ready for:

- The exact mechanism at the hardware or protocol level.
- Why the obvious fix was wrong.
- The numbers: before, after, and how you measured them.
- What you would do differently with more time.
- What you still do not know about it.

**That last one matters.** Saying "I never fully explained why it only happened on
the 5 GHz radio, and here is my best remaining hypothesis" reads as honest and
senior. Claiming complete understanding of a complex incident reads as
rehearsed.

## The other stories to have ready

Two or three short ones, 90 seconds each:

- **A time you were wrong.** Real, with consequences, and what you changed about
  how you work. Not "I care too much."
- **A disagreement with someone senior.** How you made the case, and what
  happened if you lost. Losing gracefully with your reasoning intact is a fine
  answer.
- **Something you shipped that you would build differently now.** Shows you keep
  evaluating your own work.

## Traps

- **"We" instead of "I".** Credit the team once, then say what *you* did.
- **Blaming.** Even when someone else caused it, the story is about your method.
- **No number.** "It got much faster" is not a result.
- **Too much setup.** Thirty seconds of context maximum before something happens.
- **Perfect hindsight.** Include the wrong turn. It makes the rest believable.

## Exercise

1. Pick the story. Write all three levels out in full — yes, the 40-minute one.
2. Record yourself telling level 1. Listen to it. It will be worse than you think
   the first time; that is the point of recording.
3. Draw level 2 on paper five times until the drawing is automatic.
4. Have someone technical ask you three "why" questions in a row about the
   deepest technical claim in it. If you bottom out in two, go learn the layer
   below.
5. Save all three in this directory and re-read before every loop.
