// Skeleton for 01_machine_coding_problems/09_rate_limiter_library.md.
// Read Steps 1-3 first, and 00_foundations/12_lockfree_and_threadsafe_containers.md
// for the CAS-loop pattern before attempting TokenBucket::tryAcquire.
// See 10_lld/00_plan.md, section 8.

#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace lld::rate_limiter {

using Clock = std::chrono::steady_clock;

// TODO: this is the lock-free core. tryAcquire() should need no mutex at
// all -- the CAS retry loop on tokens_ is the whole point of this problem.
class TokenBucket {
public:
    TokenBucket(double capacity, double refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond), tokens_(capacity) {}

    bool tryAcquire() { throw std::logic_error("TODO: implement TokenBucket::tryAcquire"); }

private:
    void refill() { throw std::logic_error("TODO: implement TokenBucket::refill"); }

    const double capacity_;
    const double refillPerSecond_;
    std::atomic<double> tokens_;
    std::atomic<int64_t> lastRefillMs_{0};
};

class RateLimiter {
public:
    RateLimiter(double capacity, double refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond) {}

    // TODO: use a shared_mutex for the bucket map: read-heavy lookup, rare
    // writes when a brand new key first appears.
    bool tryAcquire(const std::string&) { throw std::logic_error("TODO: implement RateLimiter::tryAcquire"); }

private:
    double capacity_, refillPerSecond_;
    std::shared_mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<TokenBucket>> buckets_;
};

}  // namespace lld::rate_limiter
