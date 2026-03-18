#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

template <typename Key,
          typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class LRUCache {
public:
    explicit LRUCache(size_t capacity)
        : capacity_(capacity) {}

    std::optional<Value> get(const Key& key) {
        auto it = items_.find(key);
        if (it == items_.end()) {
            return std::nullopt;
        }

        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    void put(const Key& key, const Value& value) {
        if (capacity_ == 0) {
            return;
        }

        auto it = items_.find(key);
        if (it != items_.end()) {
            it->second->second = value;
            order_.splice(order_.begin(), order_, it->second);
            return;
        }

        if (order_.size() >= capacity_) {
            const Key& evicted = order_.back().first;
            items_.erase(evicted);
            order_.pop_back();
        }

        order_.emplace_front(key, value);
        items_[order_.front().first] = order_.begin();
    }

    size_t size() const {
        return order_.size();
    }

    size_t capacity() const {
        return capacity_;
    }

private:
    using Entry = std::pair<Key, Value>;
    using OrderList = std::list<Entry>;
    using Iterator = typename OrderList::iterator;

    size_t capacity_;
    OrderList order_;
    std::unordered_map<Key, Iterator, Hash, KeyEqual> items_;
};
