// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <numeric>
#include <thread>
#include <vector>
#include "thread_safe_queue.h"

// Test fixture for thread_safe_queue tests
struct thread_safe_queue_test : ::testing::Test {};

// Test case for single producer, single consumer
TEST_F(thread_safe_queue_test, single_producer_single_consumer) {
    sf::thread_safe_queue<int> queue;
    std::vector<int> produced_items(100);
    std::iota(produced_items.begin(), produced_items.end(), 0);
    std::vector<int> consumed_items;

    std::jthread producer([&]() {
        for (int item: produced_items) {
            queue.push(item);
        }
        queue.close();
    });

    std::jthread consumer([&]() {
        while (auto item = queue.pop()) {
            consumed_items.push_back(item.value());
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(produced_items.size(), consumed_items.size());
    ASSERT_EQ(produced_items, consumed_items);
}

// Test case for multiple producers, single consumer
TEST_F(thread_safe_queue_test, multiple_producers_single_consumer) {
    sf::thread_safe_queue<int> queue;
    std::vector<std::jthread> producers;
    const int num_producers = 4;
    const int items_per_producer = 50;
    std::vector<int> consumed_items;
    int total_produced = 0;

    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < items_per_producer; ++j) {
                queue.push(i * items_per_producer + j);
            }
        });
    }

    std::jthread consumer([&]() {
        while (consumed_items.size() < num_producers * items_per_producer) {
            if (auto item = queue.pop()) {
                consumed_items.push_back(item.value());
            } else {
                break; // Queue closed
            }
        }
    });

    for (auto &p: producers) {
        p.join();
    }
    queue.close();
    consumer.join();

    ASSERT_EQ(consumed_items.size(), num_producers * items_per_producer);
    std::sort(consumed_items.begin(), consumed_items.end());
    for (size_t i = 0; i < consumed_items.size(); ++i) {
        ASSERT_EQ(consumed_items[i], i);
    }
}
