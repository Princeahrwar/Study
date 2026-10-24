// Skeleton for 01_machine_coding_problems/10_task_scheduler_cron.md -- the
// problem set's capstone. Read Steps 1-3 first, and make sure
// 00_foundations/11_threads_futures_and_concurrency_patterns.md's
// condition_variable material is solid before attempting waitForReady.
// See 10_lld/00_plan.md, section 8.
//
// Note: TaskQueue::push/waitForReady are no-op stubs (not throwing) rather
// than "TODO" throws, because Scheduler's constructor starts a background
// thread that calls them immediately -- an uncaught throw there would
// crash the whole program with std::terminate() the moment you construct
// a Scheduler, before you've written anything. Everything else throws, as
// in every other workspace stub.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>

namespace lld::task_scheduler {

using Clock = std::chrono::steady_clock;

struct ScheduledTask {
    uint64_t id;
    std::function<void()> callable;
    Clock::time_point nextRun;
    std::optional<std::chrono::milliseconds> interval;
    std::shared_ptr<std::atomic<bool>> cancelled;

    // Min-heap by nextRun -- note the inverted comparator.
    bool operator<(const ScheduledTask& other) const { return nextRun > other.nextRun; }
};

struct TaskHandle { uint64_t id; std::shared_ptr<std::atomic<bool>> cancelled; };

// TODO: the design question this whole problem is testing: why wait_until
// rather than wait_for, and how does a newly-pushed, sooner task wake the
// scheduler thread early?
class TaskQueue {
public:
    void push(ScheduledTask) { /* TODO: implement -- push onto heap_, notify_one() */ }
    std::optional<ScheduledTask> waitForReady(std::stop_token) { return std::nullopt; /* TODO: implement */ }

private:
    std::mutex mu_;
    std::condition_variable_any cv_;
    std::priority_queue<ScheduledTask> heap_;
};

class Scheduler {
public:
    explicit Scheduler(size_t) {
        schedulerThread_ = std::jthread([this](std::stop_token token) { run(token); });
    }

    void schedule(std::function<void()>, std::chrono::milliseconds) {
        throw std::logic_error("TODO: implement Scheduler::schedule (should return TaskHandle)");
    }
    void scheduleRecurring(std::function<void()>, std::chrono::milliseconds) {
        throw std::logic_error("TODO: implement Scheduler::scheduleRecurring (should return TaskHandle)");
    }
    void cancel(const TaskHandle& handle) { handle.cancelled->store(true); }

private:
    void run(std::stop_token token) { queue_.waitForReady(token); /* TODO: implement the real loop */ }

    TaskQueue queue_;
    std::jthread schedulerThread_;
    std::atomic<uint64_t> nextId_{0};
};

}  // namespace lld::task_scheduler
