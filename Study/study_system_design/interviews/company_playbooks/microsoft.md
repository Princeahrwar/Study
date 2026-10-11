# Microsoft

## Centre of gravity

Varies more by team than anywhere else on your list. Azure host networking and
core OS teams go deep on kernel and performance; other orgs stay broad. The
consistent thread across all of them is **pragmatism, debugging ability, and
ownership**.

## Prepare

- **Ask the recruiter which stack the team is on.** Preparing Windows kernel
  internals for a Linux host-networking team is a month wasted, and this one
  question prevents it. Do this before anything else.
- `hld/07` — the fleet and OTA material travels well here.
- `kernel_drivers/07` — observability; this org values "how would you debug it in
  production" heavily.
- Your ownership stories: something you took from broken to working and then kept
  working.

## Signature questions

- "Tell me about the worst production bug you personally root-caused. Start at
  the symptom."
- "How do you know your service is healthy?"
- "You inherit a system nobody understands. First week — what do you do?"
- Design questions that are deliberately underspecified, to see whether you scope.

## Your angle

The OTA and fleet-reliability story. Shipping software to devices you cannot
physically reach is a credibility marker, and most candidates have never done it.

## Trap

Preparing generically. The variance between orgs is the defining feature of this
loop; one recruiter conversation is worth a week of unfocused study.
