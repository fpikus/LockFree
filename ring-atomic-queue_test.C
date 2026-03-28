// Code examples for the presentation by Fedor G. Pikus on
// Modern Lock-Free Programming.
//
// MIT License
//
// Copyright (c) 2026 Fedor G. Pikus, fpikus@gmail.com
//  https://github.com/fpikus/LockFree
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "ring-atomic-queue.h"

#include <atomic>
#include <limits.h>
#include <memory>
#include <numeric>
#include <set>
#include <stdlib.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

#include <gtest/gtest.h>

//================================================================================
// Test Fixture for Key-Only Queue (int)
//================================================================================

template <typename K, typename V = void, size_t Cap = 16>
class QueueTester : public ::testing::Test {
    public:
    using queue_t = RingAtomicMapQueueMPMC<K, V>;
    static constexpr size_t capacity = Cap;
    static constexpr size_t bytes = capacity*queue_t::element_size();
    std::unique_ptr<char[]> memory_ { new char[bytes] };
    queue_t queue_ { memory_.get(), bytes };
};

using QueueTesterInt = QueueTester<int>;

TEST_F(QueueTesterInt, Construct) {
    EXPECT_TRUE(queue_.empty());
    EXPECT_EQ(capacity, queue_.capacity());
}

TEST_F(QueueTesterInt, PushOne) {
    EXPECT_TRUE(queue_.push(1));
    EXPECT_FALSE(queue_.empty());
}

TEST_F(QueueTesterInt, PopOne) {
    EXPECT_TRUE(queue_.push(1));
    EXPECT_EQ(1, queue_.pop());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, PopEmpty) {
    EXPECT_EQ(0, queue_.pop());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, FillToCapacity) {
    for (size_t i = 1; i <= capacity; ++i) {
        EXPECT_TRUE(queue_.push(i)) << "Failed to push item " << i;
    }

    EXPECT_FALSE(queue_.push(capacity + 1));
    EXPECT_FALSE(queue_.empty());
}

TEST_F(QueueTesterInt, PushPop) {
    EXPECT_TRUE(queue_.push(1));
    EXPECT_TRUE(queue_.push(2));
    EXPECT_TRUE(queue_.push(3));
    EXPECT_EQ(1, queue_.pop());
    EXPECT_EQ(2, queue_.pop());
    EXPECT_FALSE(queue_.empty());
    EXPECT_EQ(3, queue_.pop());
    EXPECT_TRUE(queue_.empty());
    EXPECT_EQ(0, queue_.pop());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, PushPopTwice) {
    EXPECT_TRUE(queue_.push(1));
    EXPECT_TRUE(queue_.push(2));
    EXPECT_TRUE(queue_.push(3));
    EXPECT_TRUE(queue_.push(4));
    EXPECT_EQ(1, queue_.pop());
    EXPECT_EQ(2, queue_.pop());
    EXPECT_TRUE(queue_.push(5));
    EXPECT_TRUE(queue_.push(6));
    EXPECT_EQ(3, queue_.pop());
    EXPECT_EQ(4, queue_.pop());
    EXPECT_EQ(5, queue_.pop());
    EXPECT_FALSE(queue_.empty());
    EXPECT_EQ(6, queue_.pop());
    EXPECT_TRUE(queue_.empty());
    EXPECT_EQ(0, queue_.pop());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, PushFull) {
    size_t i = 0;
    while (queue_.push(++i)) {};
    EXPECT_EQ(i, capacity + 1);
}

TEST_F(QueueTesterInt, PushFullPop) {
    size_t i = 0;
    while (queue_.push(++i)) {};
    EXPECT_EQ(1, queue_.pop());
    EXPECT_TRUE(queue_.push(++i));
}

TEST_F(QueueTesterInt, FifoOrder) {
    // Push a sequence of numbers.
    for (size_t i = 1; i <= capacity; ++i) {
        queue_.push(i);
    }

    // Pop them and verify they come out in the same order.
    for (size_t i = 1; i <= capacity; ++i) {
        EXPECT_EQ(int(i), queue_.pop()) << "Popped wrong item at position " << i;
    }
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, WrapAround) {
    const size_t half_cap = capacity / 2;

    // 1. Fill the queue.
    for (size_t i = 1; i <= capacity; ++i) {
        queue_.push(i);
    }

    // 2. Pop the first half.
    for (size_t i = 1; i <= half_cap; ++i) {
        EXPECT_EQ(int(i), queue_.pop());
    }
    EXPECT_FALSE(queue_.empty());

    // 3. Push more items to force the internal index to wrap around.
    for (size_t i = capacity + 1; i <= capacity + half_cap; ++i) {
        EXPECT_TRUE(queue_.push(i));
    }
    
    // 4. The queue should now be full again.
    EXPECT_FALSE(queue_.push(999));

    // 5. Pop the remaining items and verify the order.
    // The second half of the original items.
    for (size_t i = half_cap + 1; i <= capacity; ++i) {
        EXPECT_EQ(int(i), queue_.pop());
    }
    // The new items that were pushed.
    for (size_t i = capacity + 1; i <= capacity + half_cap; ++i) {
        EXPECT_EQ(int(i), queue_.pop());
    }

    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterInt, CapacityCalculation) {
    // Test that the queue correctly calculates capacity as the previous power of 2.
    // Request memory for 31 elements, which should result in a capacity of 16.
    const size_t requested_elements = 31;
    using queue_t = RingAtomicMapQueueMPMC<int>;
    const size_t bytes = requested_elements*queue_t::element_size();
    auto mem = std::make_unique<char[]>(bytes);
    queue_t q(mem.get(), bytes);

    EXPECT_EQ(16u, q.capacity());
}

//================================================================================
// Test Fixture for Key-Only Queue (Pointers)
//================================================================================

// Using raw pointers as keys to test the nullptr-as-invalid-value use case.
using QueueTesterPtr = QueueTester<int*>;

TEST_F(QueueTesterPtr, PushAndPopPointers) {
    int x = 1, y = 2, z = 3;
    
    EXPECT_TRUE(queue_.push(&x));
    EXPECT_TRUE(queue_.push(&y));
    EXPECT_TRUE(queue_.push(&z));
    EXPECT_FALSE(queue_.empty());
    
    EXPECT_EQ(&x, queue_.pop());
    EXPECT_EQ(&y, queue_.pop());
    EXPECT_EQ(&z, queue_.pop());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterPtr, PopFromEmpty) {
    // Popping from an empty queue should return nullptr.
    EXPECT_EQ(nullptr, queue_.pop());
}

//================================================================================
// Test Fixture for Key-Value Queue (int, int)
//================================================================================

using QueueTesterIntInt = QueueTester<int, int>;

TEST_F(QueueTesterIntInt, Construct) {
    EXPECT_TRUE(queue_.empty());
    EXPECT_EQ(capacity, queue_.capacity());
}

TEST_F(QueueTesterIntInt, PushOne) {
    EXPECT_TRUE(queue_.push(1, 2));
    EXPECT_FALSE(queue_.empty());
}

TEST_F(QueueTesterIntInt, PopOne) {
    EXPECT_TRUE(queue_.push(1, 2));
    int value = 0;
    EXPECT_EQ(1, queue_.pop(value));
    EXPECT_EQ(2, value);
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntInt, PopEmpty) {
    int value = 7;
    EXPECT_EQ(0, queue_.pop(value));
    EXPECT_EQ(7, value);
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Test Fixture for Key-Value Queue (int, string)
//================================================================================

using QueueTesterIntString = QueueTester<int, std::string>;

TEST_F(QueueTesterIntString, PushAndPop) {
    std::string v_in = "hello";
    bool pushed = queue_.push(1, v_in);
    EXPECT_TRUE(pushed);
    EXPECT_FALSE(queue_.empty());
    
    // Check that the original string was copied, not moved.
    EXPECT_EQ("hello", v_in);

    std::string val_out;
    int key_out = queue_.pop(val_out);
    
    EXPECT_EQ(1, key_out);
    EXPECT_EQ("hello", val_out);
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntString, PushByMove) {
    std::string v_in = "world";
    // Using std::move to test move-construction.
    EXPECT_TRUE(queue_.push(1, std::move(v_in)));
    EXPECT_FALSE(queue_.empty());
    
    // The original string should be in a valid but moved-from state (likely empty).
    EXPECT_TRUE(v_in.empty());

    std::string val_out;
    EXPECT_EQ(1, queue_.pop(val_out));
    EXPECT_EQ("world", val_out);
    EXPECT_TRUE(queue_.empty());
}


TEST_F(QueueTesterIntString, PopFromEmpty) {
    std::string val_out = "initial";
    int key_out = queue_.pop(val_out);
    
    // Popping from an empty queue should return a default key and not modify the value.
    EXPECT_EQ(0, key_out);
    EXPECT_EQ("initial", val_out);
}

TEST_F(QueueTesterIntString, FillToCapacity) {
    for (size_t i = 1; i <= capacity; ++i) {
        EXPECT_TRUE(queue_.push(i, "value_" + std::to_string(i)));
    }
    // Queue should now be full.
    EXPECT_FALSE(queue_.push(99, "should_fail"));
}

TEST_F(QueueTesterIntString, FifoOrder) {
    for (size_t i = 1; i <= capacity; ++i) {
        queue_.push(i, "value_" + std::to_string(i));
    }

    for (size_t i = 1; i <= capacity; ++i) {
        std::string val;
        EXPECT_EQ(int(i), queue_.pop(val));
        EXPECT_EQ("value_" + std::to_string(i), val);
    }
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntString, WrapAround) {
    const size_t half_cap = capacity / 2;

    // 1. Fill queue.
    for (size_t i = 1; i <= capacity; ++i) {
        queue_.push(i, "v" + std::to_string(i));
    }

    // 2. Pop first half.
    for (size_t i = 1; i <= half_cap; ++i) {
        std::string val;
        EXPECT_EQ(int(i), queue_.pop(val));
        EXPECT_EQ("v" + std::to_string(i), val);
    }

    // 3. Push more to wrap around.
    for (size_t i = capacity + 1; i <= capacity + half_cap; ++i) {
        EXPECT_TRUE(queue_.push(i, "v" + std::to_string(i)));
    }
    
    // 4. Pop remaining items and verify order.
    for (size_t i = half_cap + 1; i <= capacity + half_cap; ++i) {
        std::string val;
        EXPECT_EQ(int(i), queue_.pop(val));
        EXPECT_EQ("v" + std::to_string(i), val);
    }
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Concurrency Tests (MPMC) - Key-only queue (int)
//================================================================================
// Thread delays are too long on TAP.
static const bool running_under_TAP = ::getenv("CALJENKINS_UNIT_TESTS");

using QueueTesterIntConcurrent = QueueTester<int, void, 1024>; // Larger capacity for concurrency tests

TEST_F(QueueTesterIntConcurrent, SingleProducerSingleConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 1;
    const size_t consumer_count = 1;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::thread producer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        for (size_t j = 0; j < items_per_producer; ++j) {
            int value = j + 1;
            while (!queue_.push(value)) { std::this_thread::yield(); }
        }
        remaining_producers.fetch_sub(1, std::memory_order_release);
    });

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::thread consumer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        std::set<int> consumed_items;
        while (true) {
            int value = queue_.pop();
            if (value != 0) {
                consumed_items.insert(value);
            } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                std::this_thread::yield();
            } else {    // No producers and the queue is empty - we're done
                break;
            }
        }
        std::lock_guard g(m);
        for (int value : consumed_items) all_consumed_items.insert(value);
    });

    producer.join();
    consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntConcurrent, MultiProducerSingleConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 4;
    const size_t consumer_count = 1;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = items_per_producer*i + j + 1;
                while (!queue_.push(value)) { std::this_thread::yield(); }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::thread consumer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        std::set<int> consumed_items;
        while (true) {
            int value = queue_.pop();
            if (value != 0) {
                consumed_items.insert(value);
            } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                std::this_thread::yield();
            } else {    // No producers and the queue is empty - we're done
                break;
            }
        }
        std::lock_guard g(m);
        for (int value : consumed_items) all_consumed_items.insert(value);
    });

    for (auto& producer : producers) producer.join();
    consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntConcurrent, MultiProducerMultiConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 4;
    const size_t consumer_count = 4;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = items_per_producer*i + j + 1;
                while (!queue_.push(value)) { std::this_thread::yield(); }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            std::set<int> consumed_items;
            while (true) {
                int value = queue_.pop();
                if (value != 0) {
                    consumed_items.insert(value);
                } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                    std::this_thread::yield();
                } else {    // No producers and the queue is empty - we're done
                    break;
                }
            }
            std::lock_guard g(m);
            for (int value : consumed_items) all_consumed_items.insert(value);
            if (!running_under_TAP) EXPECT_NE(0u, consumed_items.size()) << "Consumer " << i << " did not pop any elements";
        });
    }

    for (auto& producer : producers) producer.join();
    for (auto& consumer : consumers) consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Concurrency Tests (MPMC) - Key-value queue (int, string)
//================================================================================

using QueueTesterIntStringConcurrent = QueueTester<int, std::string, 1024>; // Larger capacity for concurrency tests

TEST_F(QueueTesterIntStringConcurrent, SingleProducerSingleConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 1;
    const size_t consumer_count = 1;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::thread producer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        for (size_t j = 0; j < items_per_producer; ++j) {
            int value = j + 1;
            while (!queue_.push(value, std::to_string(value))) { std::this_thread::yield(); }
        }
        remaining_producers.fetch_sub(1, std::memory_order_release);
    });

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::thread consumer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        std::set<int> consumed_items;
        std::string s;
        while (true) {
            int value = queue_.pop(s);
            if (value != 0) {
                EXPECT_EQ(value, std::stoi(s));
                consumed_items.insert(value);
            } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                std::this_thread::yield();
            } else {    // No producers and the queue is empty - we're done
                break;
            }
        }
        std::lock_guard g(m);
        for (int value : consumed_items) all_consumed_items.insert(value);
    });

    producer.join();
    consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntStringConcurrent, MultiProducerSingleConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 4;
    const size_t consumer_count = 1;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = items_per_producer*i + j + 1;
                while (!queue_.push(value, std::to_string(value))) { std::this_thread::yield(); }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::thread consumer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
        std::set<int> consumed_items;
        std::string s;
        while (true) {
            int value = queue_.pop(s);
            if (value != 0) {
                EXPECT_EQ(value, std::stoi(s));
                consumed_items.insert(value);
            } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                std::this_thread::yield();
            } else {    // No producers and the queue is empty - we're done
                break;
            }
        }
        std::lock_guard g(m);
        for (int value : consumed_items) all_consumed_items.insert(value);
    });

    for (auto& producer : producers) producer.join();
    consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

TEST_F(QueueTesterIntStringConcurrent, MultiProducerMultiConsumer) {
    const size_t items_per_producer = 1000;
    const size_t producer_count = 4;
    const size_t consumer_count = 4;
    const size_t total_items = producer_count*items_per_producer;
    std::atomic<int> barrier {producer_count + consumer_count};
    std::atomic<int> remaining_producers {producer_count};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = items_per_producer*i + j + 1;
                while (!queue_.push(value, std::to_string(value))) { std::this_thread::yield(); }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::set<int> all_consumed_items;
    std::mutex m;       // Guards all_consumed_items
    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {} // Wait for all threads to be ready
            std::set<int> consumed_items;
            std::string s;
            while (true) {
                int value = queue_.pop(s);
                if (value != 0) {
                    EXPECT_EQ(value, std::stoi(s));
                    consumed_items.insert(value);
                } else if (remaining_producers.load(std::memory_order_acquire)) { // Producers still running, keep trying
                    std::this_thread::yield();
                } else {    // No producers and the queue is empty - we're done
                    break;
                }
            }
            std::lock_guard g(m);
            for (int value : consumed_items) all_consumed_items.insert(value);
            if (!running_under_TAP) EXPECT_NE(0u, consumed_items.size()) << "Consumer " << i << " did not pop any elements";
        });
    }

    for (auto& producer : producers) producer.join();
    for (auto& consumer : consumers) consumer.join();
    
    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Value Destructor Test
//================================================================================

struct CountedValue {
    static inline std::atomic<int> destructor_count {0};
    static inline std::atomic<int> constructor_count {0};
    int x;
    CountedValue() : x(0) { constructor_count.fetch_add(1, std::memory_order_relaxed); }
    CountedValue(int v) : x(v) { constructor_count.fetch_add(1, std::memory_order_relaxed); }
    CountedValue(const CountedValue& o) : x(o.x) { constructor_count.fetch_add(1, std::memory_order_relaxed); }
    CountedValue(CountedValue&& o) : x(o.x) { o.x = 0; constructor_count.fetch_add(1, std::memory_order_relaxed); }
    CountedValue& operator=(CountedValue&& o) { x = o.x; o.x = 0; return *this; }
    ~CountedValue() { destructor_count.fetch_add(1, std::memory_order_relaxed); }
    static void reset() { destructor_count = 0; constructor_count = 0; }
};

using QueueTesterCounted = QueueTester<int, CountedValue>;

TEST_F(QueueTesterCounted, DestructorCalledOnPop) {
    CountedValue::reset();
    // Push 3 elements (each push creates a temporary + in-place move construction).
    EXPECT_TRUE(queue_.push(1, CountedValue(10)));
    EXPECT_TRUE(queue_.push(2, CountedValue(20)));
    EXPECT_TRUE(queue_.push(3, CountedValue(30)));

    int dtors_before = CountedValue::destructor_count.load();

    // Pop all three. Each pop explicitly destructs the in-queue value.
    CountedValue v;
    EXPECT_EQ(1, queue_.pop(v));
    EXPECT_EQ(10, v.x);
    EXPECT_EQ(2, queue_.pop(v));
    EXPECT_EQ(20, v.x);
    EXPECT_EQ(3, queue_.pop(v));
    EXPECT_EQ(30, v.x);

    int dtors_after = CountedValue::destructor_count.load();
    // 3 explicit destructor calls in pop (one per slot).
    EXPECT_EQ(3, dtors_after - dtors_before);
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Move-Only Value Type Test
//================================================================================

struct MoveOnly {
    int x;
    MoveOnly() : x(0) {}
    MoveOnly(int v) : x(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) : x(o.x) { o.x = 0; }
    MoveOnly& operator=(MoveOnly&& o) { x = o.x; o.x = 0; return *this; }
};

using QueueTesterMoveOnly = QueueTester<int, MoveOnly>;

TEST_F(QueueTesterMoveOnly, PushAndPopMoveOnly) {
    EXPECT_TRUE(queue_.push(1, MoveOnly(42)));
    EXPECT_TRUE(queue_.push(2, MoveOnly(84)));

    MoveOnly v;
    EXPECT_EQ(1, queue_.pop(v));
    EXPECT_EQ(42, v.x);
    EXPECT_EQ(2, queue_.pop(v));
    EXPECT_EQ(84, v.x);
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Element Size and Alignment Tests
//================================================================================

TEST(ElementSizeTest, KeyOnly) {
    using queue_t = RingAtomicMapQueueMPMC<int>;
    EXPECT_GE(queue_t::element_size(), sizeof(std::atomic<int>));
}

TEST(ElementSizeTest, KeyValue) {
    using queue_t = RingAtomicMapQueueMPMC<int, std::string>;
    EXPECT_GE(queue_t::element_size(), sizeof(std::atomic<int>) + sizeof(std::string));
}

TEST(ElementSizeTest, CacheLineAligned) {
    using queue_t = RingAtomicMapQueueMPMC<int, void, 8, 64>;
    EXPECT_EQ(0u, queue_t::element_size() % 64u);
    EXPECT_GE(queue_t::element_size(), 64u);
}

//================================================================================
// Stress Test (many wrap-arounds)
//================================================================================

TEST_F(QueueTesterInt, StressWrapAround) {
    // Push and pop many more items than the capacity to stress wrap-around.
    const size_t total = capacity * 100;
    size_t popped = 0;
    for (size_t i = 1; i <= total; ++i) {
        while (!queue_.push(i)) {
            // Queue full, pop one to make room.
            int v = queue_.pop();
            EXPECT_NE(0, v);
            ++popped;
        }
    }
    // Drain remaining items.
    while (!queue_.empty()) {
        int v = queue_.pop();
        EXPECT_NE(0, v);
        ++popped;
    }
    EXPECT_EQ(total, popped);
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Concurrency Tests (SPMC) - Key-only queue (int)
//================================================================================

TEST_F(QueueTesterIntConcurrent, SingleProducerMultiConsumer) {
    const size_t total_items = 4000;
    const size_t consumer_count = 4;
    std::atomic<int> barrier {1 + static_cast<int>(consumer_count)};
    std::atomic<int> remaining_producers {1};

    std::thread producer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {}
        for (size_t j = 0; j < total_items; ++j) {
            int value = j + 1;
            while (!queue_.push(value)) { std::this_thread::yield(); }
        }
        remaining_producers.fetch_sub(1, std::memory_order_release);
    });

    std::set<int> all_consumed_items;
    std::mutex m;
    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            std::set<int> consumed_items;
            while (true) {
                int value = queue_.pop();
                if (value != 0) {
                    consumed_items.insert(value);
                } else if (remaining_producers.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                } else {
                    break;
                }
            }
            std::lock_guard g(m);
            for (int value : consumed_items) all_consumed_items.insert(value);
        });
    }

    producer.join();
    for (auto& consumer : consumers) consumer.join();

    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// Concurrency Tests (SPMC) - Key-value queue (int, string)
//================================================================================

TEST_F(QueueTesterIntStringConcurrent, SingleProducerMultiConsumer) {
    const size_t total_items = 4000;
    const size_t consumer_count = 4;
    std::atomic<int> barrier {1 + static_cast<int>(consumer_count)};
    std::atomic<int> remaining_producers {1};

    std::thread producer([&]() {
        barrier.fetch_sub(1, std::memory_order_relaxed);
        while (barrier.load(std::memory_order_relaxed) != 0) {}
        for (size_t j = 0; j < total_items; ++j) {
            int value = j + 1;
            while (!queue_.push(value, std::to_string(value))) { std::this_thread::yield(); }
        }
        remaining_producers.fetch_sub(1, std::memory_order_release);
    });

    std::set<int> all_consumed_items;
    std::mutex m;
    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            std::set<int> consumed_items;
            std::string s;
            while (true) {
                int value = queue_.pop(s);
                if (value != 0) {
                    EXPECT_EQ(value, std::stoi(s));
                    consumed_items.insert(value);
                } else if (remaining_producers.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                } else {
                    break;
                }
            }
            std::lock_guard g(m);
            for (int value : consumed_items) all_consumed_items.insert(value);
        });
    }

    producer.join();
    for (auto& consumer : consumers) consumer.join();

    EXPECT_EQ(total_items, all_consumed_items.size());
    EXPECT_TRUE(queue_.empty());
}

//================================================================================
// TSAN Stress Tests — high contention, long duration, sum-based integrity
//================================================================================

// Helper: run an MPMC stress test with configurable parameters, using atomic
// sum to detect duplication/loss (std::set silently deduplicates).
template <typename K, typename V, size_t Cap>
void RunMPMCStress(size_t producer_count, size_t consumer_count,
                   size_t items_per_producer) {
    using queue_t = RingAtomicMapQueueMPMC<K, V>;
    constexpr size_t bytes = Cap * queue_t::element_size();
    auto mem = std::make_unique<char[]>(bytes);
    queue_t queue(mem.get(), bytes);

    const size_t total_items = producer_count * items_per_producer;
    // Expected sum: each producer pushes values base+1 .. base+items_per_producer
    // where base = i * items_per_producer.  Total sum = sum(1..total_items).
    const int64_t expected_sum =
        static_cast<int64_t>(total_items) * (total_items + 1) / 2;

    std::atomic<int> barrier{
        static_cast<int>(producer_count + consumer_count)};
    std::atomic<int> remaining_producers{static_cast<int>(producer_count)};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = static_cast<int>(i * items_per_producer + j + 1);
                while (!queue.push(value)) {
                    std::this_thread::yield();
                }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::atomic<size_t> total_popped{0};
    std::atomic<int64_t> total_sum{0};

    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            size_t local_count = 0;
            int64_t local_sum = 0;
            while (true) {
                int value = queue.pop();
                if (value != 0) {
                    local_sum += value;
                    ++local_count;
                } else if (remaining_producers.load(
                               std::memory_order_acquire)) {
                    std::this_thread::yield();
                } else {
                    // Drain: producers done, but items may still be in queue.
                    while ((value = queue.pop()) != 0) {
                        local_sum += value;
                        ++local_count;
                    }
                    break;
                }
            }
            total_popped.fetch_add(local_count, std::memory_order_relaxed);
            total_sum.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(total_items, total_popped.load());
    EXPECT_EQ(expected_sum, total_sum.load())
        << "Sum mismatch: items were duplicated or lost";
    EXPECT_TRUE(queue.empty());
}

// --- Tiny queue (8 slots), extreme contention ---

TEST(TSANStress, TinyQueue_MPMC_4x4) {
    RunMPMCStress<int, void, 8>(4, 4, 10000);
}

TEST(TSANStress, TinyQueue_MPMC_8x8) {
    RunMPMCStress<int, void, 8>(8, 8, 5000);
}

TEST(TSANStress, TinyQueue_MPSC_4x1) {
    RunMPMCStress<int, void, 8>(4, 1, 10000);
}

TEST(TSANStress, TinyQueue_SPMC_1x4) {
    RunMPMCStress<int, void, 8>(1, 4, 40000);
}

// --- Medium queue (64 slots) ---

TEST(TSANStress, MediumQueue_MPMC_4x4) {
    RunMPMCStress<int, void, 64>(4, 4, 50000);
}

TEST(TSANStress, MediumQueue_MPMC_8x2) {
    RunMPMCStress<int, void, 64>(8, 2, 25000);
}

TEST(TSANStress, MediumQueue_MPMC_2x8) {
    RunMPMCStress<int, void, 64>(2, 8, 100000);
}

// --- Large queue (1024 slots), many threads ---

TEST(TSANStress, LargeQueue_MPMC_16x16) {
    RunMPMCStress<int, void, 1024>(16, 16, 10000);
}

TEST(TSANStress, LargeQueue_MPMC_32x32) {
    RunMPMCStress<int, void, 1024>(32, 32, 5000);
}

TEST(TSANStress, LargeQueue_MPSC_16x1) {
    RunMPMCStress<int, void, 1024>(16, 1, 10000);
}

TEST(TSANStress, LargeQueue_SPMC_1x16) {
    RunMPMCStress<int, void, 1024>(1, 16, 160000);
}

// --- Key-Value stress (int, std::string) on tiny queue ---

template <size_t Cap>
void RunMPMCStressKV(size_t producer_count, size_t consumer_count,
                     size_t items_per_producer) {
    using queue_t = RingAtomicMapQueueMPMC<int, std::string>;
    constexpr size_t bytes = Cap * queue_t::element_size();
    auto mem = std::make_unique<char[]>(bytes);
    queue_t queue(mem.get(), bytes);

    const size_t total_items = producer_count * items_per_producer;
    const int64_t expected_sum =
        static_cast<int64_t>(total_items) * (total_items + 1) / 2;

    std::atomic<int> barrier{
        static_cast<int>(producer_count + consumer_count)};
    std::atomic<int> remaining_producers{static_cast<int>(producer_count)};

    std::vector<std::thread> producers(producer_count);
    for (size_t i = 0; i != producer_count; ++i) {
        producers[i] = std::thread([&, i]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            for (size_t j = 0; j < items_per_producer; ++j) {
                int value = static_cast<int>(i * items_per_producer + j + 1);
                while (!queue.push(value, std::to_string(value))) {
                    std::this_thread::yield();
                }
            }
            remaining_producers.fetch_sub(1, std::memory_order_release);
        });
    }

    std::atomic<size_t> total_popped{0};
    std::atomic<int64_t> total_sum{0};
    std::atomic<size_t> value_mismatches{0};

    std::vector<std::thread> consumers(consumer_count);
    for (size_t i = 0; i != consumer_count; ++i) {
        consumers[i] = std::thread([&]() {
            barrier.fetch_sub(1, std::memory_order_relaxed);
            while (barrier.load(std::memory_order_relaxed) != 0) {}
            size_t local_count = 0;
            int64_t local_sum = 0;
            size_t local_mismatches = 0;
            std::string s;
            while (true) {
                int value = queue.pop(s);
                if (value != 0) {
                    if (std::to_string(value) != s) ++local_mismatches;
                    local_sum += value;
                    ++local_count;
                } else if (remaining_producers.load(
                               std::memory_order_acquire)) {
                    std::this_thread::yield();
                } else {
                    while ((value = queue.pop(s)) != 0) {
                        if (std::to_string(value) != s) ++local_mismatches;
                        local_sum += value;
                        ++local_count;
                    }
                    break;
                }
            }
            total_popped.fetch_add(local_count, std::memory_order_relaxed);
            total_sum.fetch_add(local_sum, std::memory_order_relaxed);
            value_mismatches.fetch_add(local_mismatches, std::memory_order_relaxed);
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(total_items, total_popped.load());
    EXPECT_EQ(expected_sum, total_sum.load())
        << "Sum mismatch: items were duplicated or lost";
    EXPECT_EQ(0u, value_mismatches.load())
        << "Key-value mismatch: value corruption detected";
    EXPECT_TRUE(queue.empty());
}

TEST(TSANStress, KV_TinyQueue_MPMC_4x4) {
    RunMPMCStressKV<8>(4, 4, 5000);
}

TEST(TSANStress, KV_MediumQueue_MPMC_8x8) {
    RunMPMCStressKV<64>(8, 8, 5000);
}

TEST(TSANStress, KV_LargeQueue_MPMC_16x16) {
    RunMPMCStressKV<1024>(16, 16, 5000);
}
