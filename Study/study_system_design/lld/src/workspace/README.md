# workspace/

Your own from-scratch attempts, per every tutorial's exercise 1
("implement without looking, then diff against the reference"). Each file
here is a **skeleton**: the class names, member names, and public method
signatures from that problem's tutorial ("Step 4-5" section), with every
method body replaced by `throw std::logic_error("TODO: ...")`. This
compiles cleanly as-is (run `cmake --build build` from `lld/` and confirm
`lld_workspace` builds) — the throws are markers for what to replace, not
missing declarations you need to write yourself.

## How to use one

1. Read the tutorial in `../../01_machine_coding_problems/`, up through
   "Step 3 — Class diagram," **then stop** — don't read the code yet.
2. Open the matching file here. Replace every `throw` with a real
   implementation, timed (45 minutes is the target, per
   `../../00_foundations/16_machine_coding_framework.md`).
3. Write a test file in `../../tests/workspace/` exercising your
   implementation (a few cases from the tutorial's "Step 6 — Edge cases"
   section is a good starting set).
4. Wire both into `../../tests/CMakeLists.txt` (an example block is
   commented out there) and run:
   ```bash
   cmake --build build && ctest --test-dir build --output-on-failure
   ```
5. **Only now** read the tutorial's full "Step 4-5" code block and diff
   your implementation against it. The diff is the lesson — where you
   diverged, and why, is worth writing down.

## Files

Each stub's interfaces are taken directly from its tutorial's class diagram
and Step 4 interface definitions — replacing the `throw`s is the exercise:

- `parking_lot.cpp` — `02_parking_lot.md`
- `elevator_system.cpp` — `03_elevator_system.md`
- `splitwise.cpp` — `04_splitwise_expense_sharing.md`
- `vending_machine.cpp` — `06_vending_machine.md`
- `movie_ticket_booking.cpp` — `07_movie_ticket_booking.md`
- `message_queue_pubsub.cpp` — `08_message_queue_pubsub.md`
- `rate_limiter.cpp` — `09_rate_limiter_library.md`
- `task_scheduler.cpp` — `10_task_scheduler_cron.md`

Tic-Tac-Toe and LRU Cache are not here — they're in `../../include/reference/`
as complete, tested implementations, since they're this track's first two
problems and worth seeing fully worked before attempting one cold.
