# LLD-S 05 — Hardware abstraction layers

> Prerequisite: `01_raii_and_modern_oop.md`.
> Directly relevant to your Wi-Fi platform work, and a common design round for
> embedded and driver roles.

## What a HAL is for

A HAL exists so that **hardware variation does not leak into logic**. Two tests
tell you whether yours works:

1. Can you add a new chip variant without touching the protocol code?
2. Can you unit-test the protocol code on a laptop with no hardware present?

If either answer is no, it is not an abstraction layer — it is a header of
`#ifdef`s. That framing is a good opening line in a HAL design round.

## Layering

```
   application / protocol logic        no hardware knowledge at all
   ------------------------------------------------------------------
   device model                        "a radio", "a queue", "a sensor"
   ------------------------------------------------------------------
   HAL interface                       read_reg / write_reg / dma / irq
   ------------------------------------------------------------------
   platform implementation             MMIO, SPI, I2C, or a simulator
```

The interface should be **narrow**. A HAL with forty methods is a HAL that leaked.
Most real hardware reduces to: read a register, write a register, map a buffer,
register an interrupt handler, and wait.

## Compile-time HAL: zero cost

```cpp
#include <concepts>
#include <cstdint>
#include <span>

// The contract, expressed so violations are compile errors with readable messages.
template <class T>
concept RegisterBus = requires(T bus, uint32_t addr, uint32_t val) {
    { bus.read32(addr)       } -> std::same_as<uint32_t>;
    { bus.write32(addr, val) } -> std::same_as<void>;
};

// A real MMIO implementation.
class MmioBus {
public:
    explicit MmioBus(volatile uint8_t* base) noexcept : base_(base) {}

    uint32_t read32(uint32_t off) const noexcept {
        return *reinterpret_cast<volatile const uint32_t*>(base_ + off);
    }
    void write32(uint32_t off, uint32_t v) noexcept {
        *reinterpret_cast<volatile uint32_t*>(base_ + off) = v;
    }
private:
    volatile uint8_t* base_;
};

// A test double. Same interface, no hardware, fully inspectable.
class FakeBus {
public:
    uint32_t read32(uint32_t off) const noexcept {
        auto it = regs_.find(off);
        return it == regs_.end() ? 0u : it->second;
    }
    void write32(uint32_t off, uint32_t v) noexcept {
        regs_[off] = v;
        writes_.push_back({off, v});        // assert on the sequence in tests
    }
    std::vector<std::pair<uint32_t,uint32_t>> writes_;
private:
    std::unordered_map<uint32_t, uint32_t> regs_;
};

// The logic, written once, against the concept.
template <RegisterBus Bus>
class RadioController {
public:
    explicit RadioController(Bus bus) : bus_(std::move(bus)) {}

    void set_channel(uint8_t channel) {
        bus_.write32(kRegChannel, channel);
        bus_.write32(kRegCmd, kCmdRetune);
    }

    [[nodiscard]] bool is_locked() const {
        return bus_.read32(kRegStatus) & kStatusPllLock;
    }

private:
    static constexpr uint32_t kRegChannel = 0x10;
    static constexpr uint32_t kRegStatus  = 0x14;
    static constexpr uint32_t kRegCmd     = 0x18;
    static constexpr uint32_t kCmdRetune  = 0x1;
    static constexpr uint32_t kStatusPllLock = 1u << 3;

    Bus bus_;
};
```

`RadioController<MmioBus>` compiles to direct register accesses with **no
indirection at all**, and `RadioController<FakeBus>` gives you a fully unit-
testable controller. That testability argument is usually more persuasive in a
design review than the performance one.

## Runtime HAL: when the variation is dynamic

```cpp
// Use this when the platform is chosen at runtime, crosses an ABI boundary,
// or must be swappable without recompiling.
class RegisterBusInterface {
public:
    virtual ~RegisterBusInterface() = default;
    virtual uint32_t read32(uint32_t off) const = 0;
    virtual void write32(uint32_t off, uint32_t val) = 0;
};

// Cost: one indirect call per access, and no inlining. For a register write
// that already costs hundreds of nanoseconds on a real bus, that is noise.
// For an in-loop DMA descriptor access, it is not.
```

**The decision rule.** Virtual dispatch costs a few nanoseconds. An MMIO register
access over a real bus costs hundreds of nanoseconds to microseconds. So virtual
is free *at the register level* and expensive *in a per-packet loop*. Place the
abstraction boundary where the operations are already slow.

That sentence is the whole answer to "templates or virtual?" and it generalises
well beyond HALs.

## Register definitions

```cpp
// Do not scatter magic numbers. A typed register map catches width and offset
// errors at compile time, and documents the hardware in one place.
struct RadioRegs {
    struct Channel { static constexpr uint32_t kOffset = 0x10; using type = uint32_t; };
    struct Status  { static constexpr uint32_t kOffset = 0x14; using type = uint32_t;
                     static constexpr uint32_t kPllLock = 1u << 3;
                     static constexpr uint32_t kTxBusy  = 1u << 4; };
};

template <class Reg, RegisterBus Bus>
typename Reg::type read(const Bus& bus) { return bus.read32(Reg::kOffset); }
```

Alternatives used in production: generate the register map from the vendor's
IP-XACT or SVD file, or from a small YAML description. Generating rather than
hand-writing is the answer when a reviewer asks how you keep it in sync with the
hardware team.

## Error handling across the boundary

```cpp
enum class HalError { Timeout, BusFault, NotSupported, Busy };

// Bounded wait with an injectable clock — never an unbounded spin, and never
// a hardcoded sleep, because both are untestable.
template <RegisterBus Bus, class Clock>
[[nodiscard]] Result<void, HalError>
wait_for_lock(Bus& bus, Clock& clock, std::chrono::microseconds timeout) {
    const auto deadline = clock.now() + timeout;
    while (clock.now() < deadline) {
        if (bus.read32(RadioRegs::Status::kOffset) & RadioRegs::Status::kPllLock)
            return {};
        clock.sleep_for(std::chrono::microseconds(10));
    }
    return HalError::Timeout;
}
```

Injecting the clock is the detail that makes this testable: a fake clock lets a
unit test exercise the timeout path in microseconds instead of really waiting.
Untestable timeout paths are where field bugs live.

## Designing for testability

The seams you want, and they are the same seams in any system:

1. **Hardware access** behind the bus interface. Fake it.
2. **Time** behind a clock interface. Fake it.
3. **Randomness** behind a generator you can seed.
4. **Threading** behind an executor, so tests can run synchronously.

With those four, essentially all logic becomes unit-testable with no hardware,
no sleeping, and deterministic results. Naming all four in a design round is a
strong answer to "how would you test this?"

## Interview angles

**"Design a HAL for a family of Wi-Fi chips."**
Start with the two tests at the top. Then narrow interface, compile-time vs
runtime decision with the cost reasoning, register map generation, and the four
testability seams.

**"Templates or virtual functions?"**
Where is the boundary and how slow is the underlying operation? Register access:
virtual is free. Per-packet: template.

**"How do you unit-test driver logic?"**
Fake bus, fake clock, assert on the write sequence. Say you would capture the
register transaction log and compare against an expected sequence.

**"How do you keep register definitions correct?"**
Generate from the vendor description; do not hand-maintain.

**"How do you handle a chip variant that has one extra register?"**
Trait/policy specialisation rather than an `if (variant == X)` in the logic. Then
explain why the `if` version rots.

## Exercises

1. Build the `RegisterBus` concept, an MMIO implementation, and a fake. Write a
   controller against it and unit-test the retune sequence by asserting on the
   exact write order.
2. Add a second chip variant that needs an extra calibration write. Implement it
   with a policy class rather than a conditional, and confirm the logic file is
   untouched.
3. Write `wait_for_lock` with an injected fake clock and test both the success
   and timeout paths in under a millisecond of real time.
4. Compare generated assembly for `RadioController<MmioBus>` against the virtual
   version at `-O2`. Confirm the template version has no call at all.
5. Take a real driver you have worked on and write down where its abstraction
   boundary is, and whether it passes the two tests at the top. Be honest — this
   exercise is for your own design vocabulary, and it will give you a story.
