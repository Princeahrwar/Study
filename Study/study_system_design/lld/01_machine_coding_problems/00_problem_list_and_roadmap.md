# Machine coding problem roadmap

> Prerequisite: `00_foundations/` complete, especially `16_machine_coding_framework.md`.
> Run every problem through that framework's procedure, not just the
> pattern-matching in your head — the procedure is the actual thing being
> graded.

**Status: complete.** All 36 problems in this roadmap are written, plus
Chess as an optional 37th (same learning objective as Tic-Tac-Toe, build
it for extra reps). Every "Status" column below says **Written**. This
roadmap was cross-checked against several independent, widely-used public
LLD curricula (AlgoMaster, awesome-low-level-design, InterviewBit, and
others) to confirm coverage — Tier 5 and the classic concurrency puzzles
in `00_foundations/11_threads_futures_and_concurrency_patterns.md` are
what that check surfaced as genuinely missing and worth adding. Tier 6
was added after a further cross-check against a popular YouTube LLD
course (Coder Army's "System Design Full Course" playlist) — six more
problems with a genuinely distinct teaching angle, including this
roadmap's only two dedicated Chain-of-Responsibility and Mediator
implementations.

The list is still worth reading in full even once solved — attempt each
one cold, using `00_foundations/16_machine_coding_framework.md`'s
procedure, timed, *before* reading its solution file. That's how these
solutions were themselves built, and it's a more realistic rehearsal than
reading a solution first and telling yourself you'd have gotten there.

**Scope note**: some interviewers, for entity-heavy problems (Library,
Movie Ticket Booking, Food Delivery), also want a **database schema** — an
ER diagram and normalized tables — alongside the class design. That's a
different skill (schema design, normalization, indexing) than OOD — the
LLD-scoped version of it (entities, cardinality, normalization to 3NF,
indexing a named query pattern) is covered in full in
`00_foundations/21_schema_design_interviews.md`, worked through against
this same problem set's Splitwise and Library designs; the deeper,
at-scale version (sharding, replication, storage-engine choice) still
belongs to the HLD/data-layer track.

**A note on "problem families" this roadmap deliberately doesn't expand
further**: public LLD problem lists often include many structurally
identical variants under different names — Hotel Management, Car Rental,
and Restaurant Reservation are all, underneath the surface theming, the
same shape as `11_library_management_system.md` (an inventory of
reservable/loanable units, a member/customer, a booking record) or
`02_parking_lot.md` (a pool of interchangeable resources assigned by
availability). Coffee Vending Machine is `06_vending_machine.md` with
different labels; Concert Ticket Booking is `07_movie_ticket_booking.md`
with a stage instead of a screen. **Recognising "this is the same shape as
a problem I've already solved, just re-themed" is itself a real interview
skill** — worth practicing deliberately (pick an unfamiliar-sounding
prompt and ask "which of my 30 does this actually map to?") rather than
needing a dedicated tutorial for every possible theming of the same
handful of underlying shapes.

## Recommended solve order (10-problem core sequence)

If you want one linear path through the highest-value problems rather than
working tier by tier, this is it — complexity increases progressively, and
each one deliberately reuses a pattern or data structure from an earlier
one. Do these ten first, in order, before the rest:

1. `01_tic_tac_toe.md` — pure OOP, a first taste of Strategy
2. `06_vending_machine.md` — State, in full
3. `02_parking_lot.md` — inheritance, Factory, Strategy, first concurrency extension
4. `03_elevator_system.md` — State at its hardest, scheduling algorithms, Observer-shaped dispatch
5. `07_movie_ticket_booking.md` — concurrency for real: pessimistic locking, TTL holds
6. `04_splitwise_expense_sharing.md` — Strategy plus a genuine graph algorithm
7. `05_lru_cache.md` — thread-safety extension, then LFU via `00_foundations/13`, then pluggable eviction
8. `08_message_queue_pubsub.md` — Producer-Consumer, Observer, and the lock-scope lesson that matters most in the whole set
9. `09_rate_limiter_library.md` — lock-free technique, atomics, CAS loops
10. `10_task_scheduler_cron.md` — the capstone of the core sequence: priority queue, thread pool, condition-variable scheduling, all composed together

## Extended sequence (the remaining 23, recommended order)

Once the core ten are solid, this order keeps building on what came
before rather than jumping randomly:

11. `17_bounded_blocking_queue.md` — the primitive underlying #8 and #10, made explicit and interview-ready on its own
12. `11_library_management_system.md` — pure OOD, the Book/BookCopy aggregation distinction
13. `20_atm_machine.md` — State + Command paired, rollback on partial failure
14. `18_distributed_id_generator.md` — the "no GoF pattern needed" example; bit-packing and clock-handling instead
15. `19_inmemory_kv_store_with_ttl.md` — lazy vs. active expiry, `std::any` type erasure
16. `21_airline_seat_reservation.md` — the optimistic-search/pessimistic-commit hybrid, contrasted with #5's fully-pessimistic design
17. `12_logger_framework.md` — Singleton actually justified, plus a Chain-of-Responsibility-you-correctly-don't-build
18. `13_notification_system.md` — Factory Method + honest partial-failure handling
19. `15_snake_and_ladder.md` — the "resist over-engineering" checkpoint
20. `16_food_delivery_system.md` — the widest-scope problem in the set; State machine spanning three lifecycles
21. `22_traffic_light_controller.md` — Tier 4 starts here: State + Observer with a real safety invariant
22. `23_pluggable_sensor_hal_framework.md` — Adapter + Abstract Factory, the HAL problem in interview form
23. `24_ota_update_manager.md` — the embedded capstone: A/B slots, State, Command, and the actual safety argument underneath all of it
24. `25_url_shortener.md` — foundational-difficulty, but a genuine algorithm decision (encoding scheme) most candidates handle sloppily
25. `27_inmemory_file_system.md` — the cleanest Composite pattern showcase in the set
26. `26_ride_sharing_service.md` — matching-as-a-pluggable-algorithm, structurally different from every assignment problem before it
27. `14_chess.md` — optional; same objective as #1, build for extra reps
28. `31_discount_coupon_engine.md` — Chain of Responsibility, done for real this time (contrast with #17's logger, which correctly avoids it)
29. `32_chat_room.md` — Mediator, the cleanest single-pattern showcase in Tier 6
30. `29_music_player.md` — State, Strategy, and Observer composed and kept deliberately separate
31. `28_document_editor.md` — Composite and Command combined, undo/redo as the actual center of gravity
32. `30_payment_gateway_system.md` — idempotency under a concurrent-retry race, a genuinely new concern
33. `33_dating_app.md` — the hardest of the six: two-sided asynchronous mutual-consent detection under the same race shape

## Tier 1 — Foundational (do these first)

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 1 | Tic-Tac-Toe | Strategy (win-check), simple state | **Written** → `01_tic_tac_toe.md` |
| 2 | Parking Lot | Strategy, Factory, Singleton (debated) | **Written** → `02_parking_lot.md` |
| 3 | Elevator System | State, Strategy (scheduling), Observer | **Written** → `03_elevator_system.md` |
| 4 | Splitwise / Expense Sharing | Strategy (split types), graph algorithm | **Written** → `04_splitwise_expense_sharing.md` |
| 5 | LRU Cache | Data structure design (hash map + doubly linked list); pluggable-eviction extension | **Written** → `05_lru_cache.md` |
| 6 | Library Management System | Composition/aggregation, Strategy (fines) | **Written** → `11_library_management_system.md` |
| 7 | Vending Machine | State (full machine), Strategy (payment) | **Written** → `06_vending_machine.md` |

## Tier 2 — Intermediate

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 8 | Rate Limiter Library | Lock-free/atomics (Token Bucket), reader-writer lock | **Written** → `09_rate_limiter_library.md`. Cross-reference `hld/notes/03_rate_limiters.md` for the distributed-scale algorithm survey |
| 9 | Logger Framework | Singleton (justified), Chain of Responsibility (deliberately not built), Strategy (sinks) | **Written** → `12_logger_framework.md` |
| 10 | Notification System (Email/SMS/Push) | Factory Method, honest partial-failure handling | **Written** → `13_notification_system.md` |
| 11 | Chess Game | Strategy (piece movement rules), Command (moves/undo) | **Written, optional** → `14_chess.md`. Same learning objective as Tic-Tac-Toe; build only for extra reps |
| 12 | Snake and Ladder | Composition, simple simulation, resisting over-engineering | **Written** → `15_snake_and_ladder.md` |
| 13 | Movie Ticket Booking (BookMyShow-style) | Concurrency (pessimistic locking), TTL-based holds, Strategy (pricing) | **Written** → `07_movie_ticket_booking.md` |
| 14 | Food Delivery System (Zomato/Swiggy-style) | State (order lifecycle), Strategy (assignment/pricing), Observer | **Written** → `16_food_delivery_system.md` |
| 15 | Cache with pluggable eviction (LRU/LFU/FIFO) | Strategy, extends #5 | **Written** → pluggable-eviction section of `05_lru_cache.md`; LFU itself in `00_foundations/13_specialized_data_structures_for_systems.md` |

## Tier 3 — Advanced / concurrency-heavy

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 16 | Thread-safe Bounded Blocking Queue | Two-condition-variable producer/consumer | **Written** → `17_bounded_blocking_queue.md` (full implementation in `00_foundations/10_concurrency_for_lld.md`) |
| 17 | Task Scheduler / Job Queue with priorities | Priority queue, Thread Pool, condition-variable scheduling | **Written** → `10_task_scheduler_cron.md` |
| 18 | Distributed ID Generator (Snowflake-style) | Bit-packing design, no classic GoF pattern — the "no pattern needed" example | **Written** → `18_distributed_id_generator.md` |
| 19 | In-memory Key-Value Store with TTL | Data structure design, lazy + active eviction, type erasure | **Written** → `19_inmemory_kv_store_with_ttl.md` |
| 20 | Concurrent LFU Cache | Data structure design under lock (fine-grained striping doesn't apply — explained why) | **Written** → concurrency section of `00_foundations/13_specialized_data_structures_for_systems.md` |
| 21 | ATM Machine | State, Command (transaction undo/rollback) | **Written** → `20_atm_machine.md` |
| 22 | Airline/Bus Seat Reservation | Concurrency (optimistic search + pessimistic commit), Strategy | **Written** → `21_airline_seat_reservation.md` |

## Tier 4 — Embedded/systems-flavoured LLD (your differentiator)

These aren't standard interview-prep-site problems — they're the ones worth
having ready specifically because they let you demonstrate embedded
background inside a software-LLD-shaped round, which is a strong
differentiator against candidates without it.

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 23 | Traffic Light Controller (embedded state machine) | State, Observer (sensor input), a real safety invariant | **Written** → `22_traffic_light_controller.md` |
| 24 | Pluggable Sensor/HAL Framework (multi-vendor sensors behind one interface) | Adapter, Abstract Factory, Bridge — direct tie-in to `lld/notes/05_hal_architecture.md` | **Written** → `23_pluggable_sensor_hal_framework.md` |
| 25 | Device Firmware Update (OTA) Manager | State, Command (rollback), Observer (progress), the A/B-slot safety argument | **Written** → `24_ota_update_manager.md`. Cross-reference `hld/notes/07_edge_to_cloud_fleet_management.md` for the fleet-scale version |

## Tier 5 — Frequently asked elsewhere, not yet a natural fit above

Surfaced by cross-checking this roadmap against several independent public
LLD curricula — high-frequency problems with a genuinely distinct teaching
angle from everything in Tiers 1-4, rather than a re-themed duplicate (see
the "problem families" note above for why most re-themed duplicates were
deliberately *not* added as separate tutorials).

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 26 | URL Shortener | Encoding-scheme algorithm design, Repository pattern named explicitly | **Written** → `25_url_shortener.md` |
| 27 | Ride-Sharing Service (Uber-style) | Strategy (matching algorithm) as the central extension point, not an afterthought | **Written** → `26_ride_sharing_service.md` |
| 28 | In-Memory File System | Composite pattern, as directly as this problem set ever gets | **Written** → `27_inmemory_file_system.md` |

## Tier 6 — Frequently asked elsewhere, surfaced by a YouTube-course cross-check

Surfaced by checking this roadmap against Coder Army's "System Design
Full Course" YouTube playlist (40 videos: OOP/SOLID/UML theory, every
GoF pattern including Null Object, and 15 worked "build X" projects).
Every pattern and principle it teaches was already covered; these six
projects were the genuine gaps — each centers a pattern or a design
concern (idempotency, two-sided mutual consent) not otherwise dedicated
anywhere in Tiers 1-5. One project from that same check, a quick-commerce
inventory/delivery app, was deliberately *not* added — it's a re-theme of
`11_library_management_system.md` + `02_parking_lot.md`, per the
"problem families" note above.

| # | Problem | Core pattern(s) exercised | Status |
| --- | --- | --- | --- |
| 29 | Document Editor (Google-Docs-style) | Composite (content tree) + Command (undoable edits), together | **Written** → `28_document_editor.md` |
| 30 | Music Player (Spotify-style) | State + Strategy (next-track) + Observer, three patterns deliberately kept separate | **Written** → `29_music_player.md` |
| 31 | Payment Gateway System | State (transaction lifecycle), Strategy (payment methods), idempotency-key deduplication | **Written** → `30_payment_gateway_system.md` |
| 32 | Discount Coupon Engine | Chain of Responsibility, this roadmap's one literal implementation of it | **Written** → `31_discount_coupon_engine.md` |
| 33 | Chat Room | Mediator, contrasted directly with `08_message_queue_pubsub.md`'s pub/sub shape | **Written** → `32_chat_room.md` |
| 34 | Dating App (Tinder-style) | Two-sided asynchronous mutual-match detection under a race, Strategy (recommendation) | **Written** → `33_dating_app.md` |

Also worth knowing as its own category, not a single problem: **classic
concurrency interview puzzles** (Print FooBar Alternately, Print
Zero-Even-Odd, the H2O Molecule problem) — short thread-signaling drills
distinct from the larger design problems above, covered with full
implementations in `00_foundations/11_threads_futures_and_concurrency_patterns.md`'s
"Classic concurrency interview puzzles" section.

## Company frequency notes

Loop content changes, but these leanings have been stable:

- **Google, Microsoft**: favour problems with a clean OOD story and clear
  extensibility asks — Parking Lot, Library System, Elevator, Splitwise are
  all common.
- **Meta**: LLD rounds are less universal (more coding-speed-focused
  generally), but infra/platform orgs run them — expect concurrency-flavoured
  problems (Tier 3) when they do.
- **Nvidia, Cisco, Palo Alto Networks**: when LLD is run at all here, it
  skews toward Tier 4-flavoured problems or a HAL/driver-abstraction variant
  of a Tier 2 problem (e.g. "design a pluggable transport layer" instead of
  "design a notification system" — same Strategy/Factory shape, embedded
  skin). This is exactly why Tier 4 is worth doing even though it's less
  common on generic interview-prep sites.

## How to use this now that it's complete

1. Work the 10-problem core sequence first, in order — each is
   deliberately chosen to isolate one or two patterns cleanly before
   problems start combining several at once.
2. Attempt every problem **cold** before reading its solution file, timed,
   narrated out loud — that's the actual rehearsal; reading a solution
   first teaches you to recognise a design, not to produce one under
   pressure.
3. Once you've done 6-8 problems, re-attempt the *first* one (Tic-Tac-Toe)
   cold and timed. The improvement in your speed and design instinct
   between attempt 1 and attempt 8 is the actual measure of progress in
   this track — track it.
4. Once you've done all 36 (37 with Chess), pick three at random from
   different tiers and re-attempt them cold, back to back, in one sitting
   — this is the closest rehearsal to an actual loop with multiple rounds,
   and it's the point where you'll notice which patterns are truly
   reflexive versus still requiring conscious recall.
