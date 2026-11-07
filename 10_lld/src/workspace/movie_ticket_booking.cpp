// Skeleton for 01_machine_coding_problems/07_movie_ticket_booking.md.
// Read Steps 1-3 first. This is the first concurrency-heavy problem in the
// sequence -- read 00_foundations/10_concurrency_for_lld.md before
// attempting tryHold/confirm if thread-safety under contention isn't yet
// comfortable. See 10_lld/00_plan.md, section 8.

#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace lld::movie_ticket_booking {

enum class SeatCategory { SILVER, GOLD, PREMIUM };
enum class SeatState { AVAILABLE, HELD, BOOKED };
using Clock = std::chrono::steady_clock;

struct Seat { std::string id; SeatCategory category; };
struct SeatHold { std::string id; std::vector<std::string> seatIds; std::string userId; Clock::time_point expiresAt; };
struct Booking { std::string id; std::vector<std::string> seatIds; std::string userId; double amount; };

class PricingStrategy {
public:
    virtual double priceFor(SeatCategory) const = 0;
    virtual ~PricingStrategy() = default;
};

// The concurrency-critical class. TODO: tryHold, confirm, expireStaleHolds
// -- all must be atomic across the WHOLE requested seat set, not per-seat.
class Show {
public:
    Show(std::string id, std::vector<Seat> seats) : id_(std::move(id)) {
        for (auto& s : seats) {
            seats_[s.id] = s;
            states_[s.id] = SeatState::AVAILABLE;
        }
    }

    void tryHold(const std::vector<std::string>&, const std::string&) {
        throw std::logic_error("TODO: implement Show::tryHold (should return optional<SeatHold>)");
    }
    void confirm(const std::string&, double) {
        throw std::logic_error("TODO: implement Show::confirm (should return optional<Booking>)");
    }
    void expireStaleHolds() {
        throw std::logic_error("TODO: implement Show::expireStaleHolds");
    }

private:
    std::string id_;
    std::mutex mu_;
    std::unordered_map<std::string, Seat> seats_;
    std::unordered_map<std::string, SeatState> states_;
    std::unordered_map<std::string, SeatHold> holds_;
};

}  // namespace lld::movie_ticket_booking
