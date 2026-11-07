# LLD-S 01 — RAII, ownership and modern C++ design

> Prerequisite: working C++. This tutorial is about the vocabulary a Staff-level
> reviewer expects, not language basics.

## RAII is an ownership statement, not a cleanup trick

Most people describe RAII as "the destructor frees it". That is the mechanism.
The *design* content is: **a type's constructor establishes an invariant and its
destructor releases it, so the invariant holds for the object's entire lifetime
and cannot be forgotten.**

That reframing matters because it tells you what to wrap. Anything with a
paired acquire/release — memory, fds, locks, DMA mappings, refcounts, a device
being held in reset, a tracing span — should be a type.

```cpp
#include <unistd.h>
#include <utility>
#include <system_error>

// A file descriptor owner. Move-only, because an fd has exactly one owner.
class FileDescriptor {
public:
    FileDescriptor() noexcept = default;
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}

    ~FileDescriptor() { reset(); }

    FileDescriptor(FileDescriptor&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    // Relinquish ownership without closing.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);     // deliberately ignoring close() errors here;
        fd_ = fd;                        // see the note below
    }

private:
    int fd_{-1};
};
```

Two details worth defending in review:

- **`std::exchange` in the move constructor** is the idiom: it sets the source to
  the moved-from state and returns the old value in one expression.
- **`close()` can fail**, and a destructor cannot report it. For a file whose data
  integrity matters you need an explicit `close()` method that returns an error,
  with the destructor as a backstop. Raising this unprompted is a good signal —
  it shows you know the limits of RAII rather than treating it as magic.

## The rule of zero, three/five

```
  Rule of zero    Define NO special members. Let members manage themselves.
                  This is the default and the goal.

  Rule of five    If you define ANY of destructor, copy ctor, copy assign,
                  move ctor, move assign — you probably need all five.
                  You are here only when you own a raw resource.
```

In practice: write resource wrappers (rule of five) once, then every class that
uses them follows the rule of zero. A codebase where many classes define
destructors is a codebase with insufficient wrappers.

## Ownership vocabulary

Be able to state the meaning of each in one line, because this is a standard
interview question:

| Type | Ownership | Cost | Use |
| --- | --- | --- | --- |
| `T` by value | Exclusive, on the stack | none | Default |
| `std::unique_ptr<T>` | Exclusive, heap | zero overhead | Polymorphism, large objects, PIMPL |
| `std::shared_ptr<T>` | Shared, refcounted | **atomic** inc/dec | Genuinely shared lifetime only |
| `std::weak_ptr<T>` | Non-owning observer | lock() is atomic | Break cycles, caches |
| `T&` / `T*` | **Non-owning** | none | Parameters, non-owning views |
| `std::span<T>` | Non-owning view + size | none | Array parameters |
| `std::string_view` | Non-owning string view | none | String parameters |

**`shared_ptr` is not the default.** Its refcount is atomic, so copying one in a
hot path is a coherence event on a shared cache line — exactly the false-sharing
problem of `04_computer_organization/19_cache_coherence.md`. In a data plane, a `shared_ptr` copied per packet across threads is a
measurable bottleneck. The default is `unique_ptr`, and `shared_ptr` is what you
reach for when lifetime is genuinely shared and dynamic.

Signature conventions that say what you mean:

```cpp
void consume(std::unique_ptr<Widget> w);   // takes ownership
void borrow(Widget& w);                    // does not own; must not be null
void maybe(Widget* w);                     // does not own; may be null
void read(std::span<const uint8_t> data);  // non-owning view of a buffer
```

A function signature should make ownership unambiguous without reading the body.
That claim is itself a good thing to say in a design discussion.

## Interfaces and the cost of polymorphism

```cpp
// Runtime polymorphism: one indirect call, blocks inlining. Fine at low rate.
class PacketSink {
public:
    virtual ~PacketSink() = default;
    virtual void on_packet(std::span<const uint8_t>) = 0;
};

// Compile-time polymorphism: fully inlinable, no vtable, one type per binary path.
template <class Sink>
class Pipeline {
public:
    explicit Pipeline(Sink sink) : sink_(std::move(sink)) {}
    void run(std::span<const uint8_t> p) { sink_.on_packet(p); }
private:
    Sink sink_;
};

// C++20 concepts make the template's contract explicit and its errors readable.
template <class T>
concept PacketSinkLike = requires(T t, std::span<const uint8_t> p) {
    { t.on_packet(p) } -> std::same_as<void>;
};

template <PacketSinkLike Sink>
class CheckedPipeline { /* ... */ };
```

The design rule, and it is the answer to "when do you use virtual?":

- **Runtime polymorphism** when the set of types is open, chosen at runtime, or
  crosses an ABI boundary — plugins, drivers, test doubles at low rate.
- **Compile-time polymorphism** when the set is closed and known, and the call is
  hot. You get inlining, which usually matters more than the call itself.
- **`std::variant` + `std::visit`** when the set is closed but you need a runtime
  choice, with no heap allocation.

## Error handling without exceptions

Kernel-adjacent and embedded codebases frequently disable exceptions. Know both
models and when each applies.

```cpp
#include <variant>
#include <system_error>

// A minimal expected-style result. C++23 has std::expected; this is the shape.
template <class T, class E = std::error_code>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(E error) : v_(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { return std::get<T>(v_); }
    const E& error() const& { return std::get<E>(v_); }

private:
    std::variant<T, E> v_;
};

// [[nodiscard]] is doing real work here: it turns "forgot to check the error"
// from a silent bug into a compiler warning.
[[nodiscard]] Result<FileDescriptor> open_device(const char* path);
```

The trade, stated honestly: exceptions cost nothing on the success path and are
the right default for application code; they cost binary size, need a runtime,
and make worst-case timing unbounded, which is why hard-real-time and kernel code
bans them. "It depends on whether the path is hot and whether timing must be
bounded" is the answer that scores.

## `noexcept` where it actually matters

`noexcept` on a **move constructor** is not decoration. `std::vector` will only
use moves during reallocation if the move is `noexcept`; otherwise it copies, to
preserve the strong exception guarantee. A non-noexcept move on an expensive type
silently costs you copies.

```cpp
class Buffer {
public:
    Buffer(Buffer&& o) noexcept : data_(std::exchange(o.data_, nullptr)),
                                  size_(std::exchange(o.size_, 0)) {}
    Buffer& operator=(Buffer&& o) noexcept { /* ... */ return *this; }
    // ...
};
static_assert(std::is_nothrow_move_constructible_v<Buffer>);
```

That `static_assert` is a good habit: it makes the requirement enforced rather
than hoped for.

## Practical modern features worth using

```cpp
// Designated initialisers (C++20): self-documenting aggregate construction.
struct RingConfig { size_t depth = 1024; bool huge_pages = false; int numa_node = -1; };
RingConfig cfg{ .depth = 4096, .huge_pages = true };

// Structured bindings.
auto [it, inserted] = map.try_emplace(key, value);

// if-with-initialiser: scope the variable to the check.
if (auto r = open_device("/dev/rkdma0"); !r) return r.error();

// std::byte for raw memory — expresses "these are bytes, not small integers".
std::span<const std::byte> payload;

// [[nodiscard]] on anything whose result must be checked.
[[nodiscard]] bool try_push(const T& item);
```

## Interview angles

**"What is RAII?"**
Lead with the invariant framing, not the destructor. Then give the fd example and
mention that `close()` failing is a real limit.

**"unique_ptr vs shared_ptr?"**
Exclusive vs shared, and the atomic refcount cost. Say `unique_ptr` is the
default and give a concrete reason shared_ptr hurts in a hot path.

**"Why is your move constructor noexcept?"**
So `std::vector` moves rather than copies on reallocation.

**"Exceptions or error codes?"**
Give both, and the deciding criteria: hot path, bounded timing, ABI, codebase
convention.

**"When would you avoid virtual functions?"**
Hot path with a closed type set; inlining loss usually exceeds the dispatch cost.
Offer templates or `std::variant`.

## Exercises

1. Write RAII wrappers for: a file descriptor, an `mmap` region, a POSIX timer,
   and a `pthread_mutex` lock guard. Make each move-only and `noexcept`-correct.
   These become `10_lld/include/` and you will use them in Project 03 (`pfilt`).
2. Write a class that owns a resource, then deliberately give it a
   non-`noexcept` move. Put 1000 of them in a `vector`, force reallocation, and
   count constructor calls to prove the copies happen.
3. Implement `Result<T, E>` fully with monadic helpers (`map`, `and_then`).
   Compare its assembly against an exception-based version on the success path.
4. Take a small class hierarchy with a virtual call in a loop and rewrite it three
   ways: virtual, template, `std::variant`. Benchmark all three at 10M calls.
5. Audit a piece of your own existing code for ownership clarity: can a reader
   tell from each signature who owns what? Rewrite the ambiguous ones.
