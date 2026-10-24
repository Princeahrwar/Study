// Reference implementation for 01_machine_coding_problems/05_lru_cache.md
// Faithful to the tutorial's code exactly. Templated, so it must be
// header-only -- read the .md for why std::list + std::unordered_map<Key,
// list::iterator> gives O(1) get/put (the iterator-stability argument in
// 00_foundations/02_stl_for_system_design.md is the key fact).
#pragma once

#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lld::lru_cache {

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) throw std::invalid_argument("capacity must be > 0");
    }

    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;

        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    void put(const Key& key, Value value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }

        if (items_.size() == capacity_) {
            auto& lru = items_.back();
            index_.erase(lru.first);
            items_.pop_back();
        }

        items_.emplace_front(key, std::move(value));
        index_[key] = items_.begin();
    }

    std::size_t size() const { return items_.size(); }

private:
    std::size_t capacity_;
    std::list<std::pair<Key, Value>> items_;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> index_;
};

}  // namespace lld::lru_cache
