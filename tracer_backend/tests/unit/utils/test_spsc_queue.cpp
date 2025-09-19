#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <tracer_backend/utils/spsc_queue.h>
#include <thread>
#include <atomic>

TEST(SpscQueue, spsc__basic_push_pop__then_fifo) {
    auto* q = spsc_queue_create(8);
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(spsc_queue_is_empty(q));
    // Push capacity-1 elements (single empty slot invariant)
    const uint32_t usable = spsc_queue_capacity(q) - 1;
    for (uint32_t i = 0; i < usable; ++i) {
        EXPECT_TRUE(spsc_queue_push(q, i)) << i;
    }
    // Optional: pushing more may fail if full; focus on FIFO correctness
    // Pop all and verify order
    for (uint32_t i = 0; i < usable; ++i) {
        uint32_t v = 0;
        ASSERT_TRUE(spsc_queue_pop(q, &v));
        EXPECT_EQ(v, i);
    }
    uint32_t tmp;
    EXPECT_FALSE(spsc_queue_pop(q, &tmp)); // empty now
    spsc_queue_destroy(q);
}

TEST(SpscQueue, spsc__wraparound__then_correct_indices) {
    auto* q = spsc_queue_create(4); // effective 4, usable 3
    ASSERT_NE(q, nullptr);
    uint32_t v;
    EXPECT_TRUE(spsc_queue_push(q, 1));
    EXPECT_TRUE(spsc_queue_push(q, 2));
    EXPECT_TRUE(spsc_queue_push(q, 3));
    EXPECT_FALSE(spsc_queue_push(q, 4)); // full
    EXPECT_TRUE(spsc_queue_pop(q, &v)); EXPECT_EQ(v, 1u);
    EXPECT_TRUE(spsc_queue_push(q, 4));
    EXPECT_TRUE(spsc_queue_pop(q, &v)); EXPECT_EQ(v, 2u);
    EXPECT_TRUE(spsc_queue_pop(q, &v)); EXPECT_EQ(v, 3u);
    EXPECT_TRUE(spsc_queue_pop(q, &v)); EXPECT_EQ(v, 4u);
    EXPECT_FALSE(spsc_queue_pop(q, &v)); // empty
    spsc_queue_destroy(q);
}

TEST(SpscQueue, spsc__concurrent_producer_consumer__then_progress) {
    auto* q = spsc_queue_create(1024);
    ASSERT_NE(q, nullptr);
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> produced{0}, consumed{0};
    std::thread prod([&]{
        for (uint32_t i = 0; i < 5000; ++i) {
            // Retry until push succeeds (queue has space)
            bool pushed = false;
            for (int retry = 0; retry < 10000 && !pushed; ++retry) {
                pushed = spsc_queue_push(q, i);
                if (!pushed) {
                    std::this_thread::yield();  // Give consumer a chance to consume
                }
            }
            EXPECT_TRUE(pushed);  // Should always succeed with retries
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        stop.store(true);  // Signal consumer that production is done
    });
    std::thread cons([&]{
        uint32_t v;
        uint32_t expected_value = 0;
        while (consumed.load() < 5000) {
            if (spsc_queue_pop(q, &v)) {
                EXPECT_EQ(v, expected_value++);  // Verify values in order
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (stop.load() && consumed.load() >= produced.load()) {
                // Producer finished and we consumed everything
                break;
            } else {
                std::this_thread::yield();  // Give producer a chance to produce
            }
        }
    });
    prod.join();
    cons.join();
    EXPECT_EQ(produced.load(), 5000u);
    EXPECT_EQ(consumed.load(), 5000u);
    spsc_queue_destroy(q);
}

TEST(SpscQueue, spsc__is_full__then_detects_capacity_minus_one) {
    auto* q = spsc_queue_create(8); // effective power-of-two
    ASSERT_NE(q, nullptr);
    const uint32_t usable = spsc_queue_capacity(q) - 1;
    // Initially not full
    EXPECT_FALSE(spsc_queue_is_full(q));
    for (uint32_t i = 0; i < usable; ++i) {
        ASSERT_TRUE(spsc_queue_push(q, i));
    }
    // Now queue is full (one-slot-empty invariant)
    EXPECT_TRUE(spsc_queue_is_full(q));
    // Pop one to make space then verify no longer full
    uint32_t v = 0;
    ASSERT_TRUE(spsc_queue_pop(q, &v));
    EXPECT_FALSE(spsc_queue_is_full(q));
    spsc_queue_destroy(q);
}

TEST(SpscQueue, spsc__size_estimate__then_matches_push_pop_and_wrap) {
    auto* q = spsc_queue_create(4); // effective 4, usable 3
    ASSERT_NE(q, nullptr);
    // Size starts at 0
    EXPECT_EQ(spsc_queue_size_estimate(q), 0u);
    // Push two, size becomes 2
    ASSERT_TRUE(spsc_queue_push(q, 10));
    ASSERT_TRUE(spsc_queue_push(q, 20));
    EXPECT_EQ(spsc_queue_size_estimate(q), 2u);
    // Pop one, size becomes 1
    uint32_t out = 0; ASSERT_TRUE(spsc_queue_pop(q, &out));
    EXPECT_EQ(spsc_queue_size_estimate(q), 1u);
    // Fill to capacity-1 and wrap
    ASSERT_TRUE(spsc_queue_push(q, 30));
    ASSERT_TRUE(spsc_queue_push(q, 40)); // should now be full (3 items)
    EXPECT_EQ(spsc_queue_size_estimate(q), 3u);
    // Drain all
    ASSERT_TRUE(spsc_queue_pop(q, &out));
    ASSERT_TRUE(spsc_queue_pop(q, &out));
    ASSERT_TRUE(spsc_queue_pop(q, &out));
    EXPECT_EQ(spsc_queue_size_estimate(q), 0u);
    spsc_queue_destroy(q);
}
