# Cisco

## Centre of gravity

Protocol depth, control plane vs data plane, embedded constraints, and real war
stories. You will be comfortable here — which is exactly the trap.

## Prepare

- `09_drivers/05_interrupt_handling` through `05`.
- `11_hld/03_systems_domain/01_data_plane_vs_control_plane` and `11_hld/03_systems_domain/03_edge_to_cloud_fleet_management` — the fleet and control-plane material.
- `13_embedded/01_rtos_design_constraints` — RTOS constraints, since MAC timing is firm real-time.
- Your best debugging war story, at three zoom levels (see
  `15_interviews/02_behavioral/`).

## Signature questions

- "Where does your current architecture break at ten times the client density?"
- "Walk me through a bug you root-caused that nobody else could."
- "How does your controller push config, and what happens when it is
  unreachable?"
- "Design the control plane for a 50,000-AP deployment."

## Your angle

Domain fluency is free here. Spend it on something else.

## Trap — read this twice

**The default outcome at Cisco is a lateral move at the same level.** You will
find the questions easy, answer them well, and be levelled as "a strong senior
Wi-Fi engineer" — which is what you already are.

To be levelled *up* you must volunteer the architecture axis unprompted:

- Fleet scale and the arithmetic behind it.
- Cost, in dollars, of the design you are proposing.
- Blast radius and cross-team impact.
- What you would build differently if you owned the whole system.

Comfort reads as "same job, new badge." Every time you are asked a protocol
question you find easy, answer it and then add one sentence that zooms out. That
single habit is the difference between a lateral and a promotion here.
