// Exercise 1 from 01_machine_coding_problems/05_lru_cache.md, turned into
// real assertions. Run: ctest -R LRUCache --output-on-failure
#include "reference/lru_cache.hpp"

#include <gtest/gtest.h>

using lld::lru_cache::LRUCache;

TEST(LRUCache, GetMissingKeyReturnsNullopt) {
    LRUCache<int, int> cache(2);
    EXPECT_EQ(cache.get(1), std::nullopt);
}

TEST(LRUCache, PutThenGetRoundTrips) {
    LRUCache<int, std::string> cache(2);
    cache.put(1, "one");
    ASSERT_TRUE(cache.get(1).has_value());
    EXPECT_EQ(*cache.get(1), "one");
}

TEST(LRUCache, EvictsLeastRecentlyUsedOnOverflow) {
    LRUCache<int, int> cache(2);
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);          // capacity 2: evicts key 1 (never touched since insertion)

    EXPECT_EQ(cache.get(1), std::nullopt);
    EXPECT_EQ(*cache.get(2), 200);
    EXPECT_EQ(*cache.get(3), 300);
}

TEST(LRUCache, GetRefreshesRecency) {
    LRUCache<int, int> cache(2);
    cache.put(1, 100);
    cache.put(2, 200);
    cache.get(1);                // touch key 1 -- now key 2 is the LRU one
    cache.put(3, 300);           // should evict key 2, not key 1

    EXPECT_EQ(*cache.get(1), 100);
    EXPECT_EQ(cache.get(2), std::nullopt);
    EXPECT_EQ(*cache.get(3), 300);
}

TEST(LRUCache, PutOnExistingKeyUpdatesValueAndRecency) {
    LRUCache<int, int> cache(2);
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(1, 999);           // update key 1's value -- also refreshes recency
    cache.put(3, 300);           // should evict key 2, not key 1

    EXPECT_EQ(*cache.get(1), 999);
    EXPECT_EQ(cache.get(2), std::nullopt);
}

TEST(LRUCache, RejectsZeroCapacity) {
    EXPECT_THROW((LRUCache<int, int>(0)), std::invalid_argument);
}
