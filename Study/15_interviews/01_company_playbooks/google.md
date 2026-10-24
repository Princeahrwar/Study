# Google

## Centre of gravity

Algorithmic rigour is still real at senior levels. Design rounds are graded on
**scoping and data modelling before boxes and arrows** — candidates who start
drawing architecture lose to candidates who start by bounding the problem.

## Prepare

- `11_hld/01_fundamentals/02_estimation_and_budgets` especially — the estimation procedure is exactly what this loop
  rewards, and doing it out loud in the first three minutes changes the whole
  interview.
- All of `11_hld/`.
- `10_lld/02_systems_lld/06_lld_interview_problems` for the design-and-implement rounds.
- Algorithm practice: graphs, dynamic programming, and the ability to state
  complexity precisely.

## Signature questions

- "Design the config-push system for a million devices. What is the consistency
  model?"
- "Design a distributed rate limiter." (`11_hld/01_fundamentals/15_rate_limiters` — have all three distributed
  designs.)
- "How would you store and query 1 PB of time-series data?" (`11_hld/01_fundamentals/09_storage_engines` and `06`.)
- Algorithm problems where the follow-up is always "now do it with less memory"
  or "now do it distributed."

## Your angle

**Do not open with 802.11.** Open with "a fleet of 50,000 stateful edge nodes
with intermittent connectivity" — that is a distributed systems problem they
recognise, and you have actually run one. Translate your domain into their
vocabulary and your experience suddenly counts double.

## Trap

Treating the design round as an architecture-drawing exercise. Scope first: what
are we building, for whom, at what scale, with what consistency requirement. The
arithmetic from `11_hld/01_fundamentals/02_estimation_and_budgets` is the tool.
