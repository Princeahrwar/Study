// Skeleton for 01_machine_coding_problems/04_splitwise_expense_sharing.md.
// Read that file's Steps 1-3 before filling this in. See workspace/README.md.
//
// The debt-simplification algorithm in BalanceSheet::simplify is the
// hardest part of this one -- attempt it yourself before checking the
// tutorial's greedy largest-creditor/largest-debtor approach.

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace lld::splitwise {

struct User { std::string id; std::string name; };
struct Split { User user; double amount; };

class SplitStrategy {
public:
    // TODO: design computeSplits' signature yourself (it needs a way to
    // pass exact amounts/percentages in for ExactSplit/PercentSplit), then
    // compare against the tutorial's.
    virtual std::vector<Split> computeSplits() const = 0;
    virtual ~SplitStrategy() = default;
};

struct Expense {
    User payer;
    double totalAmount;
    std::vector<Split> splits;
};

class BalanceSheet {
public:
    void update(const Expense&) { throw std::logic_error("TODO: implement BalanceSheet::update"); }

    struct Transaction { std::string from, to; double amount; };
    std::vector<Transaction> simplify() const {
        throw std::logic_error("TODO: implement BalanceSheet::simplify");
    }

private:
    std::map<std::string, std::map<std::string, double>> balances_;
};

class ExpenseManager {
public:
    void addExpense() { throw std::logic_error("TODO: implement ExpenseManager::addExpense (should return Expense)"); }
    std::vector<BalanceSheet::Transaction> settleUp() const { return sheet_.simplify(); }

private:
    BalanceSheet sheet_;
};

}  // namespace lld::splitwise
