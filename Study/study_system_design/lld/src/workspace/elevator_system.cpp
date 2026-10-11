// Skeleton for 01_machine_coding_problems/03_elevator_system.md.
// Read that file's Steps 1-3 before filling this in. See workspace/README.md.
//
// Every method below compiles but throws "not implemented" -- replace the
// throws with real logic. Don't peek at the tutorial's Step 4-5 code until
// this passes your own tests.

#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace lld::elevator_system {

enum class Direction { UP, DOWN, IDLE };

class Elevator;

class ElevatorState {
public:
    virtual void addRequest(Elevator&, int floor) = 0;
    virtual void step(Elevator&) = 0;
    virtual Direction direction() const = 0;
    virtual ~ElevatorState() = default;
};

// TODO: IdleState, MovingUpState, MovingDownState -- the LOOK-algorithm
// logic from the tutorial's Step 4-5 is the actual exercise here.
class IdleState : public ElevatorState {
public:
    void addRequest(Elevator&, int) override { throw std::logic_error("TODO: implement IdleState::addRequest"); }
    void step(Elevator&) override {}
    Direction direction() const override { return Direction::IDLE; }
};

class Elevator {
public:
    explicit Elevator(int id) : id_(id) {}

    void addRequest(int floor) { state_->addRequest(*this, floor); }
    void step() { state_->step(*this); }
    void setState(std::unique_ptr<ElevatorState> s) { state_ = std::move(s); }
    Direction direction() const { return state_->direction(); }

    int id_;
    int currentFloor_ = 0;
    std::set<int> upRequests_;
    std::set<int> downRequests_;
    std::unique_ptr<ElevatorState> state_;
};

class DispatchStrategy {
public:
    virtual Elevator* selectElevator(std::vector<std::unique_ptr<Elevator>>& elevators, int floor, Direction dir) = 0;
    virtual ~DispatchStrategy() = default;
};

class Dispatcher {
public:
    Dispatcher(int numElevators, std::unique_ptr<DispatchStrategy> strategy)
        : strategy_(std::move(strategy)) {
        for (int i = 0; i < numElevators; ++i) {
            auto e = std::make_unique<Elevator>(i);
            e->setState(std::make_unique<IdleState>());
            elevators_.push_back(std::move(e));
        }
    }

    void requestElevator(int, Direction) {
        throw std::logic_error("TODO: implement Dispatcher::requestElevator");
    }
    void tick() { for (auto& e : elevators_) e->step(); }

private:
    std::vector<std::unique_ptr<Elevator>> elevators_;
    std::unique_ptr<DispatchStrategy> strategy_;
};

}  // namespace lld::elevator_system
