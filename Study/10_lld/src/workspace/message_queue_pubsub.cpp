// Skeleton for 01_machine_coding_problems/08_message_queue_pubsub.md.
// Read Steps 1-3 first. The single highest-value detail in this problem is
// in Topic::publish -- copy the subscriber list under the lock, dispatch
// outside it. Try to get that right yourself before checking the tutorial.
// See 10_lld/00_plan.md, section 8.

#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace lld::message_queue_pubsub {

struct Message {
    std::string id;
    std::string topic;
    std::string payload;
    std::chrono::steady_clock::time_point publishedAt;
};

class Subscriber {
public:
    virtual void onMessage(const Message&) = 0;
    virtual ~Subscriber() = default;
};

class LoadBalanceStrategy {
public:
    virtual Subscriber* selectMember(std::vector<Subscriber*>& members) = 0;
    virtual ~LoadBalanceStrategy() = default;
};

// TODO: RoundRobinStrategy
class RoundRobinStrategy : public LoadBalanceStrategy {
public:
    Subscriber* selectMember(std::vector<Subscriber*>&) override {
        throw std::logic_error("TODO: implement RoundRobinStrategy::selectMember");
    }
};

class ConsumerGroup {
public:
    ConsumerGroup(std::string name, std::unique_ptr<LoadBalanceStrategy> strategy)
        : name_(std::move(name)), strategy_(std::move(strategy)) {}

    void addMember(Subscriber*) { throw std::logic_error("TODO: implement ConsumerGroup::addMember"); }
    void dispatch(const Message&) { throw std::logic_error("TODO: implement ConsumerGroup::dispatch"); }

private:
    std::string name_;
    std::mutex mu_;
    std::vector<Subscriber*> members_;
    std::unique_ptr<LoadBalanceStrategy> strategy_;
};

class Topic {
public:
    void subscribe(Subscriber*) { throw std::logic_error("TODO: implement Topic::subscribe"); }
    void subscribeGroup(const std::string&, Subscriber*) { throw std::logic_error("TODO: implement Topic::subscribeGroup"); }
    // publish() is the one to think hardest about: what happens while
    // holding the lock vs. after releasing it?
    void publish(const Message&) { throw std::logic_error("TODO: implement Topic::publish"); }

private:
    std::mutex mu_;
    std::vector<Subscriber*> broadcastSubscribers_;
    std::unordered_map<std::string, std::unique_ptr<ConsumerGroup>> groups_;
};

class Broker {
public:
    void publish(const std::string&, const std::string&) { throw std::logic_error("TODO: implement Broker::publish"); }
    void subscribe(const std::string&, Subscriber*) { throw std::logic_error("TODO: implement Broker::subscribe"); }
    void subscribeGroup(const std::string&, const std::string&, Subscriber*) {
        throw std::logic_error("TODO: implement Broker::subscribeGroup");
    }

private:
    std::mutex topicsMu_;
    std::unordered_map<std::string, std::unique_ptr<Topic>> topics_;
};

}  // namespace lld::message_queue_pubsub
