# 15 — Testing: Google Test, Google Mock, and testable design

> Prerequisite: `05_solid_principles.md` (specifically DIP),
> `14_application_architecture_patterns.md`. "How would you test this?" is
> one of the most common follow-ups across the entire problem set — this
> file is the concrete answer: the tools, and the design habits that make
> the answer easy instead of hypothetical.

## Why testability is a design property, not an afterthought

A class is only as testable as its dependencies are replaceable. Every
solution in `01_machine_coding_problems/` that depends on an **interface**
(`PricingStrategy`, `SplitStrategy`, `VendingMachineState`) rather than a
concrete class is, by construction, already testable — you inject a test
double in place of the real dependency and assert on behaviour without
touching a real clock, a real file, or a real network call. This is DIP
(`05_solid_principles.md`) cashing out as a concrete engineering payoff, not
just a principle to recite.

## Setting up Google Test (gtest)

```cpp
#include <gtest/gtest.h>

TEST(ParkingLotTest, ParksVehicleInSmallestFittingSlot) {
    ParkingLot lot = buildTestLot();   // test fixture helper, see below
    Car car("ABC-123");

    auto ticket = lot.parkVehicle(car);

    ASSERT_TRUE(ticket.has_value());
    EXPECT_EQ(ticket->slot->id(), 1);   // first compact slot in the test lot
}

TEST(ParkingLotTest, ReturnsNulloptWhenFull) {
    ParkingLot lot = buildFullTestLot();
    Car car("XYZ-999");

    auto ticket = lot.parkVehicle(car);

    EXPECT_FALSE(ticket.has_value());
}
```

**`ASSERT_*` vs `EXPECT_*`**: `ASSERT_*` aborts the current test
immediately on failure; `EXPECT_*` records the failure and continues. Use
`ASSERT_*` for a precondition the rest of the test depends on (if
`ticket.has_value()` is false, dereferencing it next would crash the test
binary, not just fail the test) — use `EXPECT_*` for independent checks you
want all reported even if one fails.

**Test fixtures** (`TEST_F`) for shared setup/teardown across multiple
tests:

```cpp
class ElevatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher_ = std::make_unique<Dispatcher>(3, std::make_unique<NearestElevatorStrategy>());
    }
    std::unique_ptr<Dispatcher> dispatcher_;
};

TEST_F(ElevatorTest, DispatchesNearestElevator) {
    dispatcher_->requestElevator(5, Direction::UP);
    // ... assertions using dispatcher_ ...
}
```

**Parameterized tests** — for the same assertion logic across a range of
inputs (particularly valuable for `SplitStrategy` implementations, where
you want the same "splits sum to total" property checked across many
input combinations):

```cpp
class SplitStrategyTest : public ::testing::TestWithParam<std::tuple<double, int>> {};

TEST_P(SplitStrategyTest, EqualSplitSumsToTotal) {
    auto [total, numParticipants] = GetParam();
    EqualSplit strategy;
    std::vector<User> users(numParticipants);
    auto splits = strategy.computeSplits(total, users, {});

    double sum = 0;
    for (auto& s : splits) sum += s.amount;
    EXPECT_NEAR(sum, total, 0.01);
}

INSTANTIATE_TEST_SUITE_P(VariousAmounts, SplitStrategyTest,
    ::testing::Values(std::make_tuple(100.0, 3), std::make_tuple(50.0, 7), std::make_tuple(10.0, 1)));
```

## Mocking with Google Mock (gmock)

**Problem**: `UserService` (`14_application_architecture_patterns.md`)
depends on a `UserRepository` interface — to unit test `UserService` in
isolation, you need a fake `UserRepository` whose behaviour you control per
test, and whose calls you can assert on (was `save()` called exactly once,
with the right argument?).

```cpp
#include <gmock/gmock.h>

class MockUserRepository : public UserRepository {
public:
    MOCK_METHOD(std::optional<User>, findById, (const std::string& id), (override));
    MOCK_METHOD(void, save, (const User&), (override));
};

TEST(UserServiceTest, RegisterUserSavesWhenNotExists) {
    auto mockRepo = std::make_shared<MockUserRepository>();

    EXPECT_CALL(*mockRepo, findById("u1"))
        .WillOnce(::testing::Return(std::nullopt));          // stub: no existing user
    EXPECT_CALL(*mockRepo, save(::testing::Field(&User::id, "u1")))
        .Times(1);                                            // assertion: save() called once, correct id

    UserService service(mockRepo);
    service.registerUser("u1", "Alice");
    // EXPECT_CALL assertions are checked automatically when mockRepo is destroyed
}

TEST(UserServiceTest, RegisterUserThrowsWhenAlreadyExists) {
    auto mockRepo = std::make_shared<MockUserRepository>();
    EXPECT_CALL(*mockRepo, findById("u1"))
        .WillOnce(::testing::Return(User{"u1", "Alice"}));    // stub: user already exists
    EXPECT_CALL(*mockRepo, save(::testing::_)).Times(0);      // assertion: save() must NOT be called

    UserService service(mockRepo);
    EXPECT_THROW(service.registerUser("u1", "Alice"), std::invalid_argument);
}
```

**What `EXPECT_CALL` is actually doing, worth stating precisely**: it's
both a **stub** (tells the mock what to return when called) and an
**assertion** (tells gmock what call pattern, and how many times, is
expected — checked automatically at the mock's destruction). This dual
role is the core gmock idiom and worth naming explicitly if asked "what's
the difference between a mock and a stub?": a stub only provides canned
answers; a mock additionally verifies it was *called* correctly — gmock's
`EXPECT_CALL` gives you both from one declaration.

**When to reach for a hand-written fake instead of gmock**: for a simple
interface with one or two methods and straightforward behaviour, a small
hand-written `InMemoryUserRepository`-style fake (real logic, just backed
by a `std::unordered_map` instead of a database) is often *more* useful
than a gmock mock — it behaves like the real thing across a whole test,
rather than needing an `EXPECT_CALL` per method per test. Reach for gmock
specifically when you need to **assert on interaction** (was this called,
how many times, with what arguments, in what order) rather than just
**state** (does the system end up in the right state) — a distinction
worth being explicit about if asked "would you mock this or fake it?"

## Testing the patterns from `07`-`09` specifically

- **Strategy** (`HourlyPricing`, `SplitStrategy`): test each implementation
  in isolation against its contract, then test the *client* class
  (`ParkingTicket`, `ExpenseManager`) with a trivial test-only strategy
  (a lambda-backed `std::function` strategy from `02_stl_for_system_design.md`
  is often the fastest test double here — no mock framework needed).
- **State** (`VendingMachineState`, `ElevatorState`): test each state
  transition directly by constructing the machine in a known state and
  asserting the resulting state after an action — this is exactly why the
  state diagram from `04_uml_and_modeling.md` is useful for testing too:
  every arrow on the diagram is a test case.
- **Observer**: test that `publish`/`notify` reaches a registered observer
  and does *not* reach an unsubscribed one — the `weak_ptr` dangling-
  reference risk flagged in `09_design_patterns_behavioral.md` is
  specifically worth a test that subscribes, destroys the observer, then
  publishes and confirms no crash.
- **Factory**: test that each type string produces an instance of the
  correct concrete type (`dynamic_cast` in the test to check, which is one
  of the few legitimate uses of `dynamic_cast` in this whole program —
  production code should almost never need it, given `03_oop_fundamentals.md`'s
  polymorphism-through-interfaces stance, but a test verifying *which*
  concrete type a factory produced is a reasonable exception).

## Testing concurrent code

Cross-reference `10_concurrency_for_lld.md`'s exercises directly — this is
worth restating as a testing-specific point: a concurrency bug that only
manifests under a specific thread interleaving **will not reliably show up
in a normal test run**. The two tools that make concurrency tests actually
mean something:

- **Stress tests**: spin up many threads doing the operation under test in
  a tight loop for a meaningful duration, and assert an invariant holds at
  the end (no lost updates, no duplicate dispensing, capacity never
  exceeded) — necessary but not sufficient, since absence of a failure in
  one run doesn't prove absence of a race.
- **ThreadSanitizer (TSan)**: compile with `-fsanitize=thread` and run the
  same stress test — TSan instruments every memory access and lock
  operation and reports data races directly, even ones that didn't cause an
  observable failure in that particular run. This is the actual
  interview-worthy answer to "how do you know your thread-safe code is
  actually correct?" — "I stress-tested it and it passed" is a weaker
  answer than "I stress-tested it under TSan."

## Interview angles

**"How would you test this class?"** Identify its dependencies; if any
aren't behind an interface, that's a DIP gap worth naming before answering
further. Then: gmock for interaction assertions, a hand-written fake for
state-based tests, parameterized tests for a property that should hold
across many inputs (like split strategies summing to the total).

**"What's the difference between a mock, a stub, and a fake?"** Stub:
canned return values, no call verification. Mock: canned values *and* call
verification (gmock's `EXPECT_CALL`). Fake: a real, working, simplified
implementation (an in-memory repository instead of a real database) —
behaves correctly across a whole test rather than being scripted call by
call.

**"How would you test that this is actually thread-safe?"** Stress test
plus ThreadSanitizer, stated as above — not "I read through the code
carefully."

## Exercises

1. Write a `TEST_F` fixture and at least three test cases for
   `10_lld/01_machine_coding_problems/01_tic_tac_toe.md`'s `Game`: a win via row, a win via diagonal, and a
   draw. Confirm each catches a deliberately introduced bug (comment out
   the diagonal check in `RowColDiagonalStrategy` and confirm the
   corresponding test fails).
2. Write a `MockUserRepository`-style gmock test for
   `10_lld/01_machine_coding_problems/04_splitwise_expense_sharing.md`'s `ExpenseManager`, asserting
   `BalanceSheet::update` behaviour indirectly through the public API,
   without a real in-memory ledger — decide, and justify in one sentence,
   whether a mock or a hand-written fake is the better fit here.
3. Write a parameterized test sweeping `PercentSplit` across several
   participant counts and total amounts, asserting the sum always matches
   the total within a small epsilon.
4. Take `10_concurrency_for_lld.md`'s `BoundedBlockingQueue` stress test
   exercise and actually run it under ThreadSanitizer
   (`g++ -fsanitize=thread -g`), first against a deliberately-broken
   version (remove the mutex) to confirm TSan reports the race, then
   against the correct version to confirm a clean run.
