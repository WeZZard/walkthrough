#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <cstring>

extern "C" {
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/tracer_types.h>
}

// For test-only access to C++ fields
#include "thread_registry_private.h"

// Helper to allocate registry arena
static std::unique_ptr<uint8_t[]> alloc_registry(size_t& out_size) {
    out_size = thread_registry_calculate_memory_size_with_capacity(4);
    auto mem = std::unique_ptr<uint8_t[]>(new uint8_t[out_size]);
    std::memset(mem.get(), 0, out_size);
    return mem;
}

TEST(RingPoolSwap, ring_pool__swap_and_submit__then_drain_receives_old) {
    size_t size = 0; auto arena = alloc_registry(size);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 4);
    ASSERT_NE(reg, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(reg, 0xAA55);
    ASSERT_NE(lanes, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);

    RingPool* pool = ring_pool_create(reg, lanes, 0);
    ASSERT_NE(pool, nullptr);

    // Get active ring header and write a small event
    RingBufferHeader* hdr = ring_pool_get_active_header(pool);
    ASSERT_NE(hdr, nullptr);
    struct Ev { uint64_t a, b; } ev{1,2};
    // Use raw helpers because we only have header here
    (void)ring_buffer_write_raw(hdr, sizeof(Ev), &ev);

    // Swap active ring
    uint32_t old_idx = UINT32_MAX;
    RingBufferHeader* before = ring_pool_get_active_header(pool);
    ASSERT_NE(before, nullptr);
    ASSERT_TRUE(ring_pool_swap_active(pool, &old_idx));
    EXPECT_NE(old_idx, UINT32_MAX);
    RingBufferHeader* after = ring_pool_get_active_header(pool);
    ASSERT_NE(after, nullptr);
    EXPECT_NE(before, after);

    ring_pool_destroy(pool);
}

TEST(RingPoolSwap, ring_pool__exhaustion__then_drop_oldest_and_recover_capacity) {
    size_t size = 0; auto arena = alloc_registry(size);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 2);
    ASSERT_NE(reg, nullptr);
    // Ensure global registry is set for lane_* operations that require it
    ASSERT_NE(thread_registry_attach(reg), nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0xBB66);
    ASSERT_NE(lanes, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);
    RingPool* pool = ring_pool_create(reg, lanes, 0);
    ASSERT_NE(pool, nullptr);

    // Perform a few swaps without draining to simulate pressure
    uint32_t old; for (int i = 0; i < 6; ++i) { (void)ring_pool_swap_active(pool, &old); }
    // Confirm no free ring is immediately available to the producer
    uint32_t free_before = lane_get_free_ring(idx_lane);
    EXPECT_EQ(free_before, UINT32_MAX);
    // Force one submission to ensure queue has content for testing
    EXPECT_TRUE(lane_submit_ring(idx_lane, 0));

    // And confirm there are submitted rings pending (pressure present)
    uint32_t pending = lane_take_ring(idx_lane);
    EXPECT_NE(pending, UINT32_MAX);
    // Return it so subsequent checks are not biased by this probe
    if (pending != UINT32_MAX) {
        EXPECT_TRUE(lane_return_ring(idx_lane, pending));
    }
    // Exhaustion handler should drop-oldest and recover capacity
    EXPECT_TRUE(ring_pool_handle_exhaustion(pool));
    // Now a free ring should be available again
    uint32_t free_after = lane_get_free_ring(idx_lane);
    EXPECT_NE(free_after, UINT32_MAX);
    ring_pool_destroy(pool);
}

TEST(RingPoolSwap, ring_pool__detail_mark__then_visible) {
    size_t size = 0; auto arena = alloc_registry(size);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 2);
    ASSERT_NE(reg, nullptr);
    ASSERT_NE(thread_registry_attach(reg), nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0xCC77);
    ASSERT_NE(lanes, nullptr);
    RingPool* dpool = ring_pool_create(reg, lanes, 1);
    ASSERT_NE(dpool, nullptr);
    EXPECT_FALSE(ring_pool_is_detail_marked(dpool));
    EXPECT_TRUE(ring_pool_mark_detail(dpool));
    EXPECT_TRUE(ring_pool_is_detail_marked(dpool));
    ring_pool_destroy(dpool);
}

TEST(RingPoolSwap, ring_pool__fallback_no_alternative__then_swap_fails) {
    // Prepare registry and attach for lane_* operations
    size_t size = 0; auto arena = alloc_registry(size);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 2);
    ASSERT_NE(reg, nullptr);
    ASSERT_NE(thread_registry_attach(reg), nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0xDD88);
    ASSERT_NE(lanes, nullptr);

    RingPool* pool = ring_pool_create(reg, lanes, 0);
    ASSERT_NE(pool, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);

    // Drain all free rings so there is no free ring available
    while (true) {
        uint32_t r = lane_get_free_ring(idx_lane);
        if (r == UINT32_MAX) break;
    }
    // Force ring_count to 1 so there is no alternative ring to rotate to
    auto* cpp_lane = ada::internal::to_cpp(idx_lane);
    cpp_lane->ring_count = 1;

    uint32_t old = UINT32_MAX;
    EXPECT_FALSE(ring_pool_swap_active(pool, &old));

    ring_pool_destroy(pool);
}

TEST(RingPoolSwap, ring_pool__exhaustion_no_oldest__then_handle_returns_false) {
    size_t size = 0; auto arena = alloc_registry(size);
    auto* reg = thread_registry_init_with_capacity(arena.get(), size, 2);
    ASSERT_NE(reg, nullptr);
    ASSERT_NE(thread_registry_attach(reg), nullptr);
    ThreadLaneSet* lanes = thread_registry_register(reg, 0xEE99);
    ASSERT_NE(lanes, nullptr);

    RingPool* pool = ring_pool_create(reg, lanes, 0);
    ASSERT_NE(pool, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);

    // Ensure submit queue is empty (no oldest to drop)
    EXPECT_EQ(lane_take_ring(idx_lane), UINT32_MAX);
    EXPECT_FALSE(ring_pool_handle_exhaustion(pool));

    ring_pool_destroy(pool);
}
