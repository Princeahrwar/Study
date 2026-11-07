# NET 05 — epoll and io_uring

> Prerequisite: `05_operating_systems/15_user_kernel_boundary.md`.

## Readiness versus completion

This is the whole conceptual difference, and everything else follows:

- **Readiness (epoll):** "tell me when this fd will not block." You are then
  woken, and you perform the operation — which is another syscall.
- **Completion (io_uring):** "perform this operation and tell me when it is
  done." The work happens without you being involved.

Readiness models cannot express file I/O well, because a regular file is always
"ready" and the read blocks anyway. That is why `epoll` never worked for disk I/O
and why applications needed thread pools for files. Completion models have no such
problem, which is arguably io_uring's most important contribution.

## epoll: the parts that matter

```c
int ep = epoll_create1(EPOLL_CLOEXEC);

struct epoll_event ev = {
    .events = EPOLLIN | EPOLLET,        /* edge-triggered */
    .data.ptr = conn,                   /* your own context pointer */
};
epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);

struct epoll_event events[256];
for (;;) {
    int n = epoll_wait(ep, events, 256, -1);
    for (int i = 0; i < n; ++i) {
        struct conn *c = events[i].data.ptr;
        handle(c, events[i].events);
    }
}
```

### Level-triggered vs edge-triggered

| | Level-triggered (default) | Edge-triggered (`EPOLLET`) |
| --- | --- | --- |
| Fires | While data remains | Only on a state *change* |
| You must | Nothing special | **Drain until `EAGAIN`** |
| Risk | More wakeups | Stalled connection if you under-read |
| Fairness | Easier | You must bound your own drain loop |

Edge-triggered is faster but unforgiving: if you read 4 KiB from a socket that
had 8 KiB and return to `epoll_wait`, **no further event arrives** and that
connection hangs until the peer sends more. The rule is absolute: with `EPOLLET`,
loop on `read` until it returns `EAGAIN`.

The counter-risk is head-of-line blocking — one very active connection can
monopolise a thread while draining. Production loops bound the drain and
re-queue, which is a nice detail to volunteer.

### Multi-threaded accept and the thundering herd

- `EPOLLEXCLUSIVE` on a listening socket wakes one waiter instead of all.
- `SO_REUSEPORT` gives each thread its **own** listening socket, and the kernel
  hashes incoming connections across them. This is usually the better design: no
  shared accept queue, no contention, and it composes with per-core sharding.

`SO_REUSEPORT` plus one epoll instance per pinned thread plus per-core state is
the standard shape of a modern high-concurrency server, and it is the same
shared-nothing pattern as `05_operating_systems/11_numa.md`.

## io_uring

```c
#include <liburing.h>

struct io_uring ring;
io_uring_queue_init(256, &ring, 0);
/* IORING_SETUP_SQPOLL: a kernel thread polls the SQ, so steady state needs
 * ZERO syscalls. Costs a core; worth it only at high rates. */

/* Submit a read. */
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(sqe, fd, buf, len, offset);
io_uring_sqe_set_data(sqe, conn);          /* your context */
io_uring_submit(&ring);                    /* one syscall for MANY sqes */

/* Reap completions. */
struct io_uring_cqe *cqe;
unsigned head;
int count = 0;
io_uring_for_each_cqe(&ring, head, cqe) {
    struct conn *c = io_uring_cqe_get_data(cqe);
    handle_completion(c, cqe->res);        /* res = return value, or -errno */
    count++;
}
io_uring_cq_advance(&ring, count);         /* batch-advance, not one at a time */
```

Features that change designs rather than just shaving syscalls:

- **Linked SQEs** (`IOSQE_IO_LINK`): chain operations so the next runs only if
  the previous succeeded. An accept-read-write chain becomes one submission.
- **Registered buffers and files**: pre-pin buffers and pre-register fds to skip
  per-operation lookup and pinning.
- **Multishot** accept and receive: one submission produces many completions.
- **Provided buffers**: the kernel picks a buffer from a pool you registered, so
  you do not need a buffer per in-flight read.
- **`SQPOLL`**: zero syscalls in steady state.

### When io_uring is not the answer

Be honest about this in interviews, because uncritical enthusiasm reads as
inexperience:

- It is a large, relatively new attack surface, and several distros disable it or
  restrict it via seccomp for that reason. "Can I actually deploy it here?" is a
  real question.
- The API is genuinely harder: ordering, buffer lifetime and cancellation are all
  your problem.
- For a small number of connections with modest rates, `epoll` is simpler and the
  difference is unmeasurable.
- Kernel version matters a lot; features landed across many releases.

## Choosing

```
  few connections, low rate            -> blocking threads. Simplest thing that works.
  many connections, network only       -> epoll + SO_REUSEPORT per core
  network + file I/O in one loop       -> io_uring (epoll cannot do files properly)
  very high rate, syscalls dominate    -> io_uring with SQPOLL
  packet processing at line rate       -> AF_XDP or DPDK (`07_zero_copy_networking.md`), not this
```

## Measuring

```bash
strace -c -f ./server              # syscalls per second, and which
perf stat -e 'syscalls:sys_enter_*' -a sleep 5
perf trace -s ./server

# Latency under concurrency:
wrk -t8 -c1000 -d30s --latency http://localhost:8080/
```

The number to watch is **syscalls per request**. If it is above two or three for
a simple echo, the event loop design is leaving performance on the table.

## Interview angles

**"epoll vs io_uring?"**
Readiness vs completion; io_uring removes the per-op syscall and handles files.
Mention SQPOLL and the deployment/security caveat.

**"Edge vs level triggered?"**
State change vs state; edge requires draining to `EAGAIN`, and under-draining
hangs the connection.

**"How do you accept connections across 8 threads?"**
`SO_REUSEPORT` with one listener and one epoll per thread. Mention
`EPOLLEXCLUSIVE` as the alternative and why REUSEPORT is usually better.

**"Why couldn't epoll do file I/O?"**
Regular files are always readable; readiness carries no information. Completion
models solve it structurally.

**"What is the thundering herd?"**
All waiters woken for one event. Fixed by `EPOLLEXCLUSIVE` or per-thread
listeners.

## Exercises

1. Write an echo server three ways: thread-per-connection, epoll level-triggered,
   epoll edge-triggered. Benchmark at 100, 1k and 10k connections. Note where
   thread-per-connection actually stops being fine — it is later than folklore says.
2. Introduce the edge-triggered bug deliberately: read once per event instead of
   draining. Reproduce the hang with a client that sends more than your read size.
3. Build an io_uring echo server with liburing. Compare syscall counts per request
   against your epoll version using `strace -c`.
4. Enable `SQPOLL` and confirm with `strace` that steady state issues no syscalls.
   Measure the CPU cost of the poll thread.
5. Compare `SO_REUSEPORT` against a single shared listener with `EPOLLEXCLUSIVE`
   at 8 threads. Measure accept throughput and per-thread fairness.
