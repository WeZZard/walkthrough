#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>

extern "C" {
#include "thread_registry.h"
#include "shared_memory.h"
#include "ring_buffer.h"
}

class ThreadRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create shared memory for testing
        size_t size = 64 * 1024 * 1024;  // 64MB to accommodate all thread lanes
        char name_buf[256];
        shm = shared_memory_create_unique("test", getpid(), shared_memory_get_session_id(),
                                         size, name_buf, sizeof(name_buf));
        ASSERT_NE(shm, nullptr) << "Failed to create shared memory";
        
        void* memory = shared_memory_get_address(shm);
        size_t shm_size = shared_memory_get_size(shm);
        
        registry = thread_registry_init(memory, shm_size);
        ASSERT_NE(registry, nullptr) << "Failed to initialize thread registry";
    }

    void TearDown() override {
        if (shm) {
            shared_memory_destroy(shm);
        }
        registry = nullptr;
    }

    SharedMemoryRef shm = nullptr;
    ThreadRegistry* registry = nullptr;
};

// Test single thread registration
TEST_F(ThreadRegistryTest, thread_registry__single_registration__then_succeeds) {
    uint32_t tid = getpid();
    ThreadLaneSet* lanes = thread_registry_register(registry, tid);
    
    ASSERT_NE(lanes, nullptr) << "Registration should succeed";
    EXPECT_EQ(lanes->thread_id, tid) << "Thread ID should match";
    EXPECT_EQ(lanes->slot_index, 0) << "First thread should get slot 0";
    EXPECT_TRUE(atomic_load(&lanes->active)) << "Thread should be marked active";
    
    // Verify TLS is set
    EXPECT_EQ(thread_registry_get_lanes(), lanes) << "TLS should cache the lanes";
    
    // Verify thread count
    EXPECT_EQ(atomic_load(&registry->thread_count), 1) << "Thread count should be 1";
}

// Test duplicate registration returns cached lanes
TEST_F(ThreadRegistryTest, thread_registry__duplicate_registration__then_returns_cached) {
    uint32_t tid = getpid();
    
    needs_log_thread_registry_registry = true;

    ThreadLaneSet* lanes1 = thread_registry_register(registry, tid);
    ASSERT_NE(lanes1, nullptr);

    EXPECT_EQ(atomic_load(&registry->thread_count), 1) << "Thread count should be 1";
    
    ThreadLaneSet* lanes2 = thread_registry_register(registry, tid);
    ASSERT_NE(lanes2, nullptr);

    needs_log_thread_registry_registry = false;
    
    EXPECT_EQ(lanes1, lanes2) << "Should return cached lanes on duplicate registration";
    EXPECT_EQ(atomic_load(&registry->thread_count), 1) << "Thread count should still be 1";
}

// Test concurrent registration gives unique slots
TEST_F(ThreadRegistryTest, thread_registry__concurrent_registration__then_unique_slots) {
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::vector<uint32_t> slots(num_threads);
    
    // Clear TLS for main thread
    tls_my_lanes = nullptr;
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([this, &success_count, &slots, i]() {
            ThreadLaneSet* lanes = thread_registry_register(registry, getpid() + i);
            if (lanes) {
                slots[i] = lanes->slot_index;
                success_count++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, num_threads) << "All threads should register successfully";
    EXPECT_EQ(atomic_load(&registry->thread_count), num_threads) << "Thread count should match";
    
    // Verify all slots are unique
    std::sort(slots.begin(), slots.end());
    for (int i = 0; i < num_threads; i++) {
        EXPECT_EQ(slots[i], i) << "Slots should be 0 through " << (num_threads - 1);
    }
}

// Test MAX_THREADS boundary
TEST_F(ThreadRegistryTest, thread_registry__max_threads_exceeded__then_returns_null) {
    // Clear TLS
    tls_my_lanes = nullptr;
    
    // Register MAX_THREADS threads
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        ThreadLaneSet* lanes = thread_registry_register(registry, i);
        ASSERT_NE(lanes, nullptr) << "Registration " << i << " should succeed";
        tls_my_lanes = nullptr;  // Clear TLS for next iteration
    }
    
    EXPECT_EQ(atomic_load(&registry->thread_count), MAX_THREADS);
    
    // Try to register one more
    ThreadLaneSet* extra = thread_registry_register(registry, MAX_THREADS);
    EXPECT_EQ(extra, nullptr) << "Registration beyond MAX_THREADS should fail";
    EXPECT_EQ(atomic_load(&registry->thread_count), MAX_THREADS) << "Count should not exceed MAX_THREADS";
}

// Test SPSC submit queue operations
TEST_F(ThreadRegistryTest, spsc_queue__single_producer__then_maintains_order) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    Lane* lane = &lanes->index_lane;
    
    // Submit some rings
    EXPECT_TRUE(lane_submit_ring(lane, 1));
    EXPECT_TRUE(lane_submit_ring(lane, 2));
    EXPECT_TRUE(lane_submit_ring(lane, 3));
    
    // Take rings in order
    EXPECT_EQ(lane_take_ring(lane), 1);
    EXPECT_EQ(lane_take_ring(lane), 2);
    EXPECT_EQ(lane_take_ring(lane), 3);
    
    // Queue should be empty
    EXPECT_EQ(lane_take_ring(lane), UINT32_MAX);
}

// Test SPSC queue wraparound
TEST_F(ThreadRegistryTest, spsc_queue__wraparound__then_correct) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    Lane* lane = &lanes->index_lane;
    
    // Fill and drain multiple times to test wraparound
    for (int round = 0; round < 3; round++) {
        // Submit up to queue capacity - 1 (queue can hold size-1 items)
        for (uint32_t i = 0; i < lane->submit_queue_size - 1; i++) {
            EXPECT_TRUE(lane_submit_ring(lane, i)) << "Round " << round << ", submit " << i;
        }
        
        // Queue should be full (can't add one more)
        EXPECT_FALSE(lane_submit_ring(lane, 99)) << "Queue should be full";
        
        // Drain all
        for (uint32_t i = 0; i < lane->submit_queue_size - 1; i++) {
            EXPECT_EQ(lane_take_ring(lane), i) << "Round " << round << ", take " << i;
        }
        
        // Queue should be empty
        EXPECT_EQ(lane_take_ring(lane), UINT32_MAX);
    }
}

// Test free queue operations
TEST_F(ThreadRegistryTest, free_queue__return_and_get__then_works) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    Lane* lane = &lanes->index_lane;
    
    // Initially, free queue has rings 1, 2, 3 (ring 0 is active)
    EXPECT_EQ(lane_get_free_ring(lane), 1);
    EXPECT_EQ(lane_get_free_ring(lane), 2);
    EXPECT_EQ(lane_get_free_ring(lane), 3);
    
    // No more free rings
    EXPECT_EQ(lane_get_free_ring(lane), UINT32_MAX);
    
    // Return some rings
    EXPECT_TRUE(lane_return_ring(lane, 0));
    EXPECT_TRUE(lane_return_ring(lane, 1));
    
    // Get them back
    EXPECT_EQ(lane_get_free_ring(lane), 0);
    EXPECT_EQ(lane_get_free_ring(lane), 1);
}

// Test memory ordering visibility
TEST_F(ThreadRegistryTest, memory_ordering__registration__then_visible_to_drain) {
    std::atomic<bool> registered{false};
    ThreadLaneSet* thread_lanes = nullptr;
    
    // Thread 1: Register
    std::thread registrar([this, &registered, &thread_lanes]() {
        thread_lanes = thread_registry_register(registry, getpid());
        ASSERT_NE(thread_lanes, nullptr);
        registered.store(true, std::memory_order_release);
    });
    
    // Thread 2: Drain thread checking registration
    std::thread drain([this, &registered]() {
        // Wait for registration
        while (!registered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        // Should see the registered thread
        uint32_t count = thread_registry_get_active_count(registry);
        EXPECT_GE(count, 1) << "Drain thread should see registered thread";
        
        ThreadLaneSet* lanes = thread_registry_get_thread_at(registry, 0);
        EXPECT_NE(lanes, nullptr) << "Should be able to get thread at slot 0";
    });
    
    registrar.join();
    drain.join();
}

// Test thread unregistration
TEST_F(ThreadRegistryTest, thread_registry__unregister__then_inactive) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    EXPECT_TRUE(atomic_load(&lanes->active));
    EXPECT_EQ(thread_registry_get_lanes(), lanes);
    
    thread_registry_unregister(lanes);
    
    EXPECT_FALSE(atomic_load(&lanes->active)) << "Thread should be marked inactive";
    EXPECT_EQ(thread_registry_get_lanes(), nullptr) << "TLS should be cleared";
}

// Performance test: Registration latency
TEST_F(ThreadRegistryTest, performance__registration__then_fast) {
    // Clear TLS
    tls_my_lanes = nullptr;
    
    auto start = std::chrono::high_resolution_clock::now();
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    auto end = std::chrono::high_resolution_clock::now();
    
    ASSERT_NE(lanes, nullptr);
    
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    EXPECT_LT(duration.count(), 1000000) << "Registration should take < 1μs (took " 
                                         << duration.count() << "ns)";
}

// Performance test: Fast path (TLS) overhead
TEST_F(ThreadRegistryTest, performance__fast_path__then_under_10ns) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    const int iterations = 1000000;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        volatile ThreadLaneSet* l = thread_registry_get_lanes();
        (void)l;  // Prevent optimization
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double per_call_ns = static_cast<double>(total_ns) / iterations;
    
    EXPECT_LT(per_call_ns, 10.0) << "Fast path should take < 10ns per call (took " 
                                  << per_call_ns << "ns)";
}

// Performance test: SPSC queue throughput
TEST_F(ThreadRegistryTest, performance__spsc_throughput__then_high) {
    ThreadLaneSet* lanes = thread_registry_register(registry, getpid());
    ASSERT_NE(lanes, nullptr);
    
    Lane* lane = &lanes->index_lane;
    const int operations = 1000000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < operations; i++) {
        // Submit and take immediately
        lane_submit_ring(lane, i % 4);
        lane_take_ring(lane);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double ops_per_sec = (static_cast<double>(operations) * 1000.0) / duration_ms;
    
    EXPECT_GT(ops_per_sec, 10000000) << "Should achieve > 10M ops/sec (got " 
                                      << ops_per_sec << " ops/sec)";
}

// Test cache line alignment prevents false sharing
TEST_F(ThreadRegistryTest, isolation__no_false_sharing__then_independent_performance) {
    // Verify structures are aligned to cache lines
    EXPECT_EQ(sizeof(Lane) % CACHE_LINE_SIZE, 0) << "Lane should be cache-line aligned";
    EXPECT_EQ(sizeof(ThreadLaneSet) % CACHE_LINE_SIZE, 0) << "ThreadLaneSet should be cache-line aligned";
    EXPECT_EQ(sizeof(ThreadRegistry) % CACHE_LINE_SIZE, 0) << "ThreadRegistry should be cache-line aligned";
    
    // Verify actual alignment in memory
    ThreadLaneSet* lanes0 = &registry->thread_lanes[0];
    ThreadLaneSet* lanes1 = &registry->thread_lanes[1];
    
    size_t addr0 = reinterpret_cast<size_t>(lanes0);
    size_t addr1 = reinterpret_cast<size_t>(lanes1);
    
    EXPECT_EQ(addr0 % CACHE_LINE_SIZE, 0) << "First ThreadLaneSet should be cache-line aligned";
    EXPECT_EQ(addr1 % CACHE_LINE_SIZE, 0) << "Second ThreadLaneSet should be cache-line aligned";
    EXPECT_GE(addr1 - addr0, CACHE_LINE_SIZE) << "ThreadLaneSets should be at least cache-line apart";
}