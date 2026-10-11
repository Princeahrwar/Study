// Skeleton for 01_machine_coding_problems/02_parking_lot.md.
// Read that file's Steps 1-3 before filling this in. See workspace/README.md
// for how this workspace is meant to be used.
//
// Every method below compiles (so `cmake --build` is green out of the box)
// but throws "not implemented" -- that's the marker for what to replace.
// Don't peek at the tutorial's Step 4-5 code block until you've filled
// these in and written your own passing tests.

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lld::parking_lot {

enum class SlotSize { MOTORCYCLE, COMPACT, LARGE };

class Vehicle {
public:
    explicit Vehicle(std::string plate) : plate_(std::move(plate)) {}
    virtual SlotSize requiredSize() const = 0;
    const std::string& plate() const { return plate_; }
    virtual ~Vehicle() = default;
private:
    std::string plate_;
};

// TODO: Motorcycle, Car, Truck subclasses -- each just overrides requiredSize().
class Car : public Vehicle {
public:
    using Vehicle::Vehicle;
    SlotSize requiredSize() const override { throw std::logic_error("TODO: implement Car::requiredSize"); }
};

class Slot {
public:
    Slot(int id, SlotSize size) : id_(id), size_(size) {}

    bool fits(const Vehicle&) const { throw std::logic_error("TODO: implement Slot::fits"); }
    void occupy() { throw std::logic_error("TODO: implement Slot::occupy"); }
    void vacate() { throw std::logic_error("TODO: implement Slot::vacate"); }
    int id() const { return id_; }

private:
    int id_;
    SlotSize size_;
    bool occupied_ = false;
};

class Level {
public:
    Level(int id, std::vector<Slot> slots) : id_(id), slots_(std::move(slots)) {}

    Slot* findAvailableSlot(const Vehicle&) {
        throw std::logic_error("TODO: implement Level::findAvailableSlot");
    }

private:
    int id_;
    std::vector<Slot> slots_;
};

class PricingStrategy {
public:
    // TODO: design the signature yourself first (what does it need to
    // compute a price?), then compare it to the tutorial's Ticket-based one.
    virtual double calculate() const = 0;
    virtual ~PricingStrategy() = default;
};

class ParkingLot {
public:
    ParkingLot(std::vector<Level> levels, std::unique_ptr<PricingStrategy> pricing)
        : levels_(std::move(levels)), pricing_(std::move(pricing)) {}

    void parkVehicle(const Vehicle&) {
        throw std::logic_error("TODO: implement ParkingLot::parkVehicle (should return optional<Ticket>)");
    }
    double unparkVehicle() {
        throw std::logic_error("TODO: implement ParkingLot::unparkVehicle (should take a Ticket)");
    }

private:
    std::vector<Level> levels_;
    std::unique_ptr<PricingStrategy> pricing_;
};

}  // namespace lld::parking_lot
