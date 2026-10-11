// Skeleton for 01_machine_coding_problems/06_vending_machine.md.
// Draw the state diagram from 00_foundations/04_uml_and_modeling.md's
// exercise 4 BEFORE filling this in -- that's the point of this problem.
// See workspace/README.md for how this workspace is meant to be used.

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace lld::vending_machine {

enum class Coin { NICKEL = 5, DIME = 10, QUARTER = 25, DOLLAR = 100 };

struct Product { std::string name; int priceCents; };

class Inventory {
public:
    void addSlot(const std::string&, Product, int) { throw std::logic_error("TODO: implement Inventory::addSlot"); }
    bool inStock(const std::string&) const { throw std::logic_error("TODO: implement Inventory::inStock"); }
    const Product& product(const std::string&) const { throw std::logic_error("TODO: implement Inventory::product"); }
    void decrement(const std::string&) { throw std::logic_error("TODO: implement Inventory::decrement"); }

private:
    std::map<std::string, std::pair<Product, int>> slots_;
};

class VendingMachine;

class VendingMachineState {
public:
    virtual void insertCoin(VendingMachine&, Coin) = 0;
    virtual void selectProduct(VendingMachine&, const std::string& code) = 0;
    virtual void refund(VendingMachine&) = 0;
    virtual ~VendingMachineState() = default;
};

// TODO: IdleState, HasMoneyState, DispensingState -- the three transitions
// that matter most: insufficient funds (stay in HasMoney), out of stock
// (auto-refund to Idle), and sufficient funds (go to Dispensing).
class IdleState : public VendingMachineState {
public:
    void insertCoin(VendingMachine&, Coin) override { throw std::logic_error("TODO: implement IdleState::insertCoin"); }
    void selectProduct(VendingMachine&, const std::string&) override { throw std::logic_error("insert coins first"); }
    void refund(VendingMachine&) override {}
};

class VendingMachine {
public:
    explicit VendingMachine(Inventory inv) : inventory_(std::move(inv)), state_(std::make_unique<IdleState>()) {}

    void insertCoin(Coin c) { state_->insertCoin(*this, c); }
    void selectProduct(const std::string& code) { state_->selectProduct(*this, code); }
    void refund() { state_->refund(*this); }
    void setState(std::unique_ptr<VendingMachineState> s) { state_ = std::move(s); }

    int insertedCents_ = 0;
    Inventory inventory_;
    std::unique_ptr<VendingMachineState> state_;
};

}  // namespace lld::vending_machine
