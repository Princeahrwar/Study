# OS 18 — IPC mechanisms and their real costs

> Prerequisite: `19_user_kernel_boundary.md` is a useful companion; read either order.

## The decision table

Pick by the shape of the problem, not by habit. Approximate round-trip costs on
a modern x86 server; treat them as ratios, not absolutes.

| Mechanism | Round trip | Ordering | Across hosts? | Natural use |
| --- | --- | --- | --- | --- |
| Shared memory + atomics | 50-200 ns | you provide it | no | Highest throughput, same machine |
| `eventfd` / futex wake | 2-5 us | kernel | no | Wakeup signalling beside shared memory |
| Unix domain socket | 5-15 us | stream or datagram | no | General local RPC, fd passing |
| Pipe / FIFO | 5-15 us | stream | no | Simple, ubiquitous |
| POSIX message queue | 5-15 us | priority-ordered | no | Bounded queues with priority |
| Netlink | 10-30 us | datagram | no | Kernel to userspace config/events |
| Loopback TCP | 20-50 us | stream | yes (same API) | Portability over speed |
| Signals | 2-10 us | none | no | Notification only; avoid for data |

The structural insight: **everything except shared memory costs at least one
syscall pair plus a copy.** So the design question is not "which IPC is fastest"
but "how do I avoid needing IPC on the hot path at all."

## Shared memory: the only option for a data plane

```cpp
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

// POSIX shared memory. Both processes map the same physical pages, so after
// setup there are no syscalls and no copies on the data path.
class SharedRegion {
public:
    // Creator.
    static SharedRegion create(const char* name, size_t bytes) {
        int fd = ::shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (fd < 0) throw std::system_error(errno, std::system_category(), "shm_open");
        if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            ::close(fd); ::shm_unlink(name);
            throw std::system_error(errno, std::system_category(), "ftruncate");
        }
        return SharedRegion(fd, bytes, name, /*owner=*/true);
    }

    // Attacher.
    static SharedRegion attach(const char* name, size_t bytes) {
        int fd = ::shm_open(name, O_RDWR, 0);
        if (fd < 0) throw std::system_error(errno, std::system_category(), "shm_open");
        return SharedRegion(fd, bytes, name, /*owner=*/false);
    }

    void* data() const noexcept { return addr_; }
    size_t size() const noexcept { return size_; }

    ~SharedRegion() {
        if (addr_ && addr_ != MAP_FAILED) ::munmap(addr_, size_);
        if (fd_ >= 0) ::close(fd_);
        if (owner_) ::shm_unlink(name_.c_str());   // RAII: the creator cleans up
    }

    SharedRegion(SharedRegion&& o) noexcept
        : fd_(o.fd_), addr_(o.addr_), size_(o.size_),
          name_(std::move(o.name_)), owner_(o.owner_) {
        o.fd_ = -1; o.addr_ = nullptr; o.owner_ = false;
    }
    SharedRegion(const SharedRegion&) = delete;
    SharedRegion& operator=(const SharedRegion&) = delete;

private:
    SharedRegion(int fd, size_t bytes, const char* name, bool owner)
        : fd_(fd), size_(bytes), name_(name), owner_(owner) {
        addr_ = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr_ == MAP_FAILED) {
            ::close(fd_);
            if (owner_) ::shm_unlink(name);
            throw std::system_error(errno, std::system_category(), "mmap");
        }
    }

    int fd_{-1};
    void* addr_{nullptr};
    size_t size_{0};
    std::string name_;
    bool owner_{false};
};
```

Place the SPSC ring from `08_memory_barriers.md` in that region and you have a zero-syscall,
zero-copy channel between processes.

**The three problems shared memory creates**, and you must have answers:

1. **Synchronisation is yours.** Atomics in shared memory work, but only if the
   type is lock-free (`std::atomic<T>::is_always_lock_free`) — a non-lock-free
   atomic uses a process-local lock table and silently fails across processes.
2. **Pointers do not travel.** The region may map at different addresses in each
   process. Store **offsets**, not pointers. Or map with `MAP_FIXED_NOREPLACE` at
   an agreed address, which is fragile.
3. **Crash safety.** If a process dies holding a lock, the region is wedged.
   Robust mutexes (`PTHREAD_MUTEX_ROBUST` + `pthread_mutex_consistent`) exist, or
   design so that a reader crash is harmless — which is one more argument for
   lock-free single-writer structures.

## eventfd: the missing piece

Shared memory has no way to say "there is work". Busy-polling burns a core;
sleeping needs a wakeup primitive. `eventfd` is the clean one, because it is a
file descriptor and therefore composes with `epoll`:

```cpp
#include <sys/eventfd.h>

int efd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

// Producer, after pushing to the shared ring:
uint64_t one = 1;
::write(efd, &one, sizeof(one));      // cheap; coalesces if consumer is behind

// Consumer, in its epoll loop:
uint64_t count;
::read(efd, &count, sizeof(count));   // returns the accumulated count, resets to 0
drain_ring();
```

The hybrid production pattern is worth naming explicitly: **spin briefly, then
sleep.** Poll the ring for a few microseconds; if nothing arrives, arm the
eventfd and block. You get low latency under load and no wasted CPU when idle.
Proposing this unprompted is a strong signal in a data-plane interview.

## Futex: what mutexes are built from

```
  futex(uaddr, FUTEX_WAIT, expected, ...)   // sleep only if *uaddr == expected
  futex(uaddr, FUTEX_WAKE, n, ...)          // wake up to n waiters
```

The design insight is that **the uncontended path never enters the kernel**: a
mutex lock is an atomic CAS in userspace, and only contention calls `futex`. This
is why an uncontended `std::mutex` lock/unlock is ~20 ns while a contended one is
microseconds, and it is a very common interview question.

## Unix domain sockets and fd passing

Their unique capability is passing **file descriptors** between processes via
`SCM_RIGHTS`. That is how a privileged process can open a device, socket or
hugepage file and hand the fd to an unprivileged worker — the basis of privilege
separation in browsers, container runtimes, and DPDK-style setups where a
control process opens resources for data-plane workers.

```cpp
#include <sys/socket.h>
#include <sys/uio.h>
#include <cstring>

// Sending an fd. The payload is in the ancillary data, not the iovec.
// The single dummy byte is required: some platforms will not deliver
// ancillary data attached to a zero-length message.
bool send_fd(int sock, int fd_to_send) {
    char dummy = 'x';
    iovec io{ &dummy, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))] = {};
    msghdr msg{};
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = cbuf; msg.msg_controllen = sizeof(cbuf);

    cmsghdr* cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type  = SCM_RIGHTS;
    cm->cmsg_len   = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cm), &fd_to_send, sizeof(int));

    return ::sendmsg(sock, &msg, 0) == 1;
}
```

## Netlink

The kernel-to-userspace channel for configuration and events: routes, links,
addresses, netfilter, and generic netlink for subsystem-specific protocols.
Relevant to you directly — `iproute2`, `wpa_supplicant` and `hostapd` all speak
netlink (nl80211 for Wi-Fi).

Key properties: datagram-based, supports multicast groups so several listeners
get the same event, and is the *correct* answer when an interviewer asks how a
driver notifies userspace of asynchronous events. Saying "netlink, or a sysfs
`poll()`-able attribute for simple cases" covers the ground well.

## What to avoid

- **Signals for data.** Async-signal-safety makes handlers nearly useless, they
  coalesce, and they interrupt syscalls. Use `signalfd` to turn them into fd
  events if you must handle them in an event loop.
- **System V IPC** (`shmget`, `msgget`, `semget`). Persistent kernel objects,
  awkward namespace, no fd semantics, leaks after crashes. POSIX equivalents are
  strictly better. Know they exist because old code uses them.
- **Loopback TCP for local-only traffic**, unless you need the portability. Unix
  sockets are faster and give you fd passing and peer credentials.

## Interview angles

**"How would two processes exchange a million messages per second?"**
Shared-memory ring, lock-free, plus eventfd for wakeups with a spin-then-sleep
policy. Do the arithmetic: at 1 M/s you have 1 us per message, and a syscall pair
is already several microseconds — so any syscall-per-message design is excluded
before you start. Leading with that arithmetic is the strong move.

**"Why is an uncontended mutex fast?"**
Futex: userspace CAS on the fast path, kernel only on contention.

**"How do you hand a socket to another process?"**
`SCM_RIGHTS` over a Unix domain socket.

**"What breaks when you put a `std::mutex` in shared memory?"**
It is not necessarily process-shared, and a crash while holding it wedges
everyone. Use `PTHREAD_PROCESS_SHARED` with `PTHREAD_MUTEX_ROBUST`, or avoid
locks entirely.

**"Pointers in shared memory?"**
Offsets, because the base address differs per process.

## Exercises

1. Build a shared-memory SPSC ring between two processes using the class above
   plus the ring from `08_memory_barriers.md`. Measure messages per second and p99 latency.
2. Add eventfd wakeups and implement spin-then-sleep. Sweep the spin duration
   from 0 to 100 us and plot latency against CPU utilisation. Find the knee.
3. Compare the same workload over: shared memory, Unix socket, pipe, loopback
   TCP. Produce the latency table for *your* machine and keep it.
4. Implement fd passing: a parent opens a file, passes the fd, the child reads
   from it. Then extend it to pass a socket.
5. Kill the producer process mid-run and observe what your consumer does. Then
   design and implement the recovery. This is the part real systems get wrong.
