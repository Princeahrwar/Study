# 20 — API design interviews

> Prerequisite: `03`-`09`, `16`. A genuinely distinct interview format from
> everything else in this track — no implementation at all, 30-45 minutes,
> graded entirely on the **shape of a public interface**: method
> signatures, naming, error-handling contracts, and how well it holds up
> against a requirement you haven't been told yet. Read `16_machine_coding_framework.md`'s
> "which of the five LLD interview types" section first if you haven't —
> this file is the dedicated treatment of type 4 from that list.

## What's actually being graded, and why it's harder than it looks

An API design round gives you no room to hide behind working code — there's
no `main()` to run, no test suite to point at. Every signal comes from the
interface itself: **would another engineer, reading only your method
signatures and their names, know exactly how to use this correctly, and
find it hard to use incorrectly?** That second half — hard to misuse — is
the part most candidates under-invest in, and it's the actual
differentiator this format is testing for.

## The checklist, each item cross-referencing where you've already built the underlying skill

**1. Small, focused interfaces — ISP, made literal.** A `Cache` interface
with 15 methods, half of which only some implementations support, is
exactly what `00_foundations/05_solid_principles.md`'s Interface
Segregation section warns against. Split by genuine capability
(`Readable`, `Writable`) rather than bundling everything one
implementation happens to support.

**2. Depend on, and expose, abstractions — DIP, made literal.** A method
signature that takes a concrete `FileLogger` instead of a `LogSink`
interface (`12_logger_framework.md`) forces every caller into your
specific implementation. In an API-design round specifically, this
extends to return types too: prefer returning an interface/abstract type
over a concrete class where callers shouldn't depend on which concrete
type they got.

**3. Avoid the boolean-parameter trap.**

```cpp
// Bad: what do true, false mean at the call site? Unreadable without
// checking the declaration every time.
void createUser(std::string name, bool isAdmin, bool sendEmail);
createUser("Alice", true, false);   // true, false -- true, false what?

// Better: named, self-documenting at the call site
struct UserOptions { bool isAdmin = false; bool sendWelcomeEmail = true; };
void createUser(std::string name, UserOptions options = {});
createUser("Alice", {.isAdmin = true, .sendWelcomeEmail = false});
```

This is a direct, practical extension of `00_foundations/07_design_patterns_creational.md`'s
Builder section — a small options struct (sometimes called a "parameter
object") solves the same call-site-readability problem a fluent builder
does, with less machinery, and is usually the right choice for 2-4
optional parameters specifically.

**4. A consistent, honest error-handling contract.** Pick one policy per
API and hold it throughout — don't mix exceptions and error-code returns
within the same interface for similar failure classes. `00_foundations/14_application_architecture_patterns.md`'s
Result-pattern section already gives you the actual rule: exceptions for
genuine errors, `std::optional`/`std::expected` for expected absence
— apply it here as an interface-wide *contract* you state explicitly
("every method in this class throws `std::invalid_argument` on bad input
and returns `std::nullopt`, never throws, for a not-found lookup") rather
than leaving callers to discover it by reading implementations.

**5. `const`-correctness as part of the contract, not an afterthought.**
A method that doesn't need to modify state but isn't marked `const`
(`cpp_primer/06_const_correctness.md`) is lying about its own contract —
callers holding a `const YourType&` can't call it even though nothing
about it actually requires mutation. In an API-design round, marking
every non-mutating method `const` is a small, visible, free signal of
care.

**6. Name for the reader who's never seen the implementation.** `get()`
on five different classes meaning five different things (a lookup that
can fail vs. one that always succeeds vs. one with a side effect) is a
consistency failure. Prefer names that encode the contract:
`find(key)` returning `optional<Value>` reads as "might not exist";
`at(key)` throwing on missing reads as "should exist, this is a bug if
not" — the same distinction `std::map::find` vs `std::map::at` already
makes in the standard library, worth citing as a precedent if asked why
you chose one name over another.

**7. Design for extension without breaking existing callers.** The direct
API-design translation of OCP (`00_foundations/05_solid_principles.md`):
adding a new optional parameter with a default doesn't break existing
call sites; adding a new required parameter, or changing a return type,
does. If a follow-up asks "how would you add X without breaking anyone
already using this API," a defaulted parameter or an overload is usually
the answer; silently changing an existing signature's meaning never is.

## A worked example: designing a small `Cache<K, V>` library's public API

Not implementing it — this is the API-design-round version of the problem,
signatures and contracts only:

```cpp
template <typename K, typename V>
class Cache {
public:
    // Construction: capacity is required (no sensible default for "how
    // much memory should this use"), eviction policy is optional with a
    // stated default -- a parameter object per item 3 above, since a
    // third or fourth tuning knob is a very plausible future addition.
    struct Options {
        EvictionPolicy policy = EvictionPolicy::LRU;
        std::optional<std::chrono::milliseconds> defaultTtl = std::nullopt;
    };
    explicit Cache(size_t capacity, Options options = {});

    // Contract, stated explicitly rather than left implicit: get() never
    // throws, returns nullopt for "not present or expired" -- an expected
    // outcome, per item 4 above.
    std::optional<V> get(const K& key) const;   // const: doesn't mutate LRU
                                                    // order from the caller's
                                                    // perspective -- an honest
                                                    // API hides that internal
                                                    // bookkeeping detail

    // put() also never throws for a normal insert; it CAN throw
    // std::invalid_argument for a genuinely invalid key/value (caller
    // programming error), per the same stated contract.
    void put(K key, V value);
    void put(K key, V value, std::chrono::milliseconds ttl);   // overload,
                                                                  // not a
                                                                  // boolean
                                                                  // "hasTtl" flag

    // erase() reports whether something was actually removed, rather
    // than silently succeeding either way -- callers who care can check;
    // callers who don't can ignore the return value, [[nodiscard]] is
    // deliberately NOT used here since ignoring it is a legitimate use.
    bool erase(const K& key);

    size_t size() const;
    void clear();

private:
    // implementation detail, never part of the discussion in this format
};
```

**Why `get()` is `const` even though a real LRU cache's `get()` mutates
internal recency-tracking state**: this is a genuine, common point of
friction worth naming if asked — `const` in C++ describes *caller-
observable* state, not literal bit-for-bit immutability. The
`mutable` keyword (briefly: a member marked `mutable` can be modified
even inside a `const` method) is the standard escape hatch for exactly
this case — internal bookkeeping that doesn't change the object's
externally-visible behavior. Naming `mutable` here, precisely, is a
strong signal in this specific interview format.

## Interview angles

**"Design the public API for X" vs. "design X"** — the single most
important thing to notice in the prompt itself. The former wants
signatures, contracts, and naming, defended verbally, no implementation.
Starting to write method *bodies* in this format is a scope
misread — catch yourself and ask "should I be implementing this, or is
the interface itself the deliverable?" if you're unsure.

**"How would a caller misuse this API?"** — a question worth asking
yourself proactively, unprompted, for every method you propose. If the
honest answer is "call it with these two booleans backward and nothing
stops them," that's exactly the boolean-trap problem from item 3, and
naming the fix before being asked is a strong signal.

**"How would you version this API without breaking existing users?"** —
name additive change (new optional parameter, new overload, new method)
as safe, and any change to an existing required parameter's type or
meaning as breaking — and that breaking changes need a new major version
or a new method name, not a silent signature change.

## Exercises

1. Take `Cache<K, V>`'s API above and design the public API (signatures
   and stated contracts only, no bodies) for a small `TaskQueue` library
   — `enqueue`, `dequeue`, a way to check size, a way to signal shutdown.
   State each method's error-handling contract explicitly before writing
   its signature.
2. Find a boolean-parameter API in code you've written recently (or in a
   standard library function you've used — several exist even in widely-
   used libraries) and redesign it using a parameter object, per item 3.
3. Take one of this program's already-implemented classes (`RateLimiter`
   from `09_rate_limiter_library.md` is a good candidate) and write out
   its *public API surface only* — every public method's signature and a
   one-line stated contract — as if you were presenting it in a 30-minute
   API-design round, with no reference to its actual implementation.
