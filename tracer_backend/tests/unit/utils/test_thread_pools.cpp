#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <cstring>
#include <atomic>

extern "C" {
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_pools.h>
}

// Test hooks overriding weak symbols in ring_pool.cpp when ADA_TESTING is enabled
static std::atomic<int> g_fail_create_lane{-1}; // -1 = no fail; 0=index; 1=detail; 2=fail both
static std::atomic<int> g_destroy_count{0};

extern "C" bool ada_test_should_fail_ring_pool_create(int lane_type) {
    int v = g_fail_create_lane.load(std::memory_order_relaxed);
    if (v == -1) return false;
    if (v == 2) return true;
    return v == lane_type;
}

extern "C" void ada_test_on_ring_pool_destroy(int /*lane_type*/) {
    g_destroy_count.fetch_add(1, std::memory_order_relaxed);
}

// Helper to allocate registry arena
static std::unique_ptr<uint8_t[]> alloc_registry(size_t& out_size, uint32_t cap = 4) {
    out_size = thread_registry_calculate_memory_size_with_capacity(cap);
    auto mem = std::unique_ptr<uint8_t[]>(new uint8_t[out_size]);
    std::memset(mem.get(), 0, out_size);
    return mem;
}

TEST(ThreadPools, thread_pools__create_and_getters__then_index_and_detail_non_null) {
    size_t size = 0; auto arena = alloc_registry(size, 4);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 4);
    ASSERT_NE(reg, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(reg, 0x1234);
    ASSERT_NE(lanes, nullptr);

    ThreadPools* pools = thread_pools_create(reg, lanes);
    ASSERT_NE(pools, nullptr);

    RingPool* idx = thread_pools_get_index_pool(pools);
    RingPool* det = thread_pools_get_detail_pool(pools);
    ASSERT_NE(idx, nullptr);
    ASSERT_NE(det, nullptr);

    // Active ring headers for both lanes should be available
    RingBufferHeader* ih = ring_pool_get_active_header(idx);
    RingBufferHeader* dh = ring_pool_get_active_header(det);
    EXPECT_NE(ih, nullptr);
    EXPECT_NE(dh, nullptr);

    thread_pools_destroy(pools);
}

TEST(ThreadPools, thread_pools__null_inputs__then_create_and_getters_null) {
    size_t size = 0; auto arena = alloc_registry(size, 2);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 2);
    ASSERT_NE(reg, nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0xABCD);
    ASSERT_NE(lanes, nullptr);

    // Null registry or lanes lead to null pools
    EXPECT_EQ(thread_pools_create(nullptr, lanes), nullptr);
    EXPECT_EQ(thread_pools_create(reg, nullptr), nullptr);

    // Getters are null-safe
    EXPECT_EQ(thread_pools_get_index_pool(nullptr), nullptr);
    EXPECT_EQ(thread_pools_get_detail_pool(nullptr), nullptr);

    // Destroy is null-safe
    thread_pools_destroy(nullptr);
}

TEST(ThreadPools, thread_pools__ring_pool_create_detail_fails__then_index_destroyed_and_null_return) {
    // Arrange: normal registry/lanes
    size_t size = 0; auto arena = alloc_registry(size, 4);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 4);
    ASSERT_NE(reg, nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0x5555);
    ASSERT_NE(lanes, nullptr);

    // Cause detail lane creation (lane_type=1) to fail
    g_fail_create_lane.store(1, std::memory_order_relaxed);
    g_destroy_count.store(0, std::memory_order_relaxed);

    // Act: creating thread pools should clean up index pool and return null
    ThreadPools* pools = thread_pools_create(reg, lanes);
    EXPECT_EQ(pools, nullptr);
    EXPECT_EQ(g_destroy_count.load(std::memory_order_relaxed), 1) << "index_pool must be destroyed";

    // Reset hook
    g_fail_create_lane.store(-1, std::memory_order_relaxed);
}

TEST(ThreadPools, thread_pools__ring_pool_create_index_fails__then_detail_destroyed_and_null_return) {
    // Arrange: normal registry/lanes
    size_t size = 0; auto arena = alloc_registry(size, 4);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 4);
    ASSERT_NE(reg, nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0x7777);
    ASSERT_NE(lanes, nullptr);

    // Cause index lane creation (lane_type=0) to fail
    g_fail_create_lane.store(0, std::memory_order_relaxed);
    g_destroy_count.store(0, std::memory_order_relaxed);

    // Act: creating thread pools should return null; detail pool must be destroyed
    ThreadPools* pools = thread_pools_create(reg, lanes);
    EXPECT_EQ(pools, nullptr);
    EXPECT_EQ(g_destroy_count.load(std::memory_order_relaxed), 1) << "detail_pool must be destroyed";

    // Reset hook
    g_fail_create_lane.store(-1, std::memory_order_relaxed);
}
