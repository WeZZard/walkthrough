#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/thread_registry.h>
#include "thread_registry_private.h"

struct AdaRingPool {
    ::ThreadRegistry* reg;
    ::ThreadLaneSet* lanes;
    int lane_type; // 0 = index, 1 = detail
};

extern "C" {

#ifdef ADA_TESTING
// Test hooks: default weak definitions allow tests to override behavior
extern "C" bool ada_test_should_fail_ring_pool_create(int lane_type) __attribute__((weak));
extern "C" void ada_test_on_ring_pool_destroy(int lane_type) __attribute__((weak));
// Provide default implementations when tests don't override
bool ada_test_should_fail_ring_pool_create(int) { return false; }
void ada_test_on_ring_pool_destroy(int) {}
#endif

RingPool* ring_pool_create(ThreadRegistry* registry, ThreadLaneSet* lanes, int lane_type) {
    if (!registry || !lanes) return nullptr;
    if (lane_type != 0 && lane_type != 1) return nullptr;
#ifdef ADA_TESTING
    if (ada_test_should_fail_ring_pool_create(lane_type)) {
        return nullptr;
    }
#endif
    auto* p = new (std::nothrow) AdaRingPool{registry, lanes, lane_type};
    return reinterpret_cast<RingPool*>(p);
}

void ring_pool_destroy(RingPool* pool) {
    if (!pool) return;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
#ifdef ADA_TESTING
    ada_test_on_ring_pool_destroy(p->lane_type);
#endif
    delete p;
}

bool ring_pool_swap_active(RingPool* pool, uint32_t* out_old_idx) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    Lane* lane = (p->lane_type == 0) ? thread_lanes_get_index_lane(p->lanes)
                                     : thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return false;

    // Get a free ring; if none, signal exhaustion
    uint32_t new_idx = lane_get_free_ring(lane);
    if (new_idx == UINT32_MAX) {
        // Fallback: if queues are not initialized yet, rotate to next ring deterministically.
        auto* cpp_lane_fallback = ada::internal::to_cpp(lane);
        if (cpp_lane_fallback->ring_count > 1) {
            uint32_t cur = cpp_lane_fallback->active_idx.load(std::memory_order_acquire);
            new_idx = (cur + 1) % cpp_lane_fallback->ring_count;
        } else {
            return false; // pool truly has no alternative
        }
    }

    // Swap active atomically
    auto* cpp_lane = ada::internal::to_cpp(lane);
    uint32_t old_idx = cpp_lane->active_idx.exchange(new_idx, std::memory_order_acq_rel);
    if (out_old_idx) *out_old_idx = old_idx;

    // Submit old ring for draining
    bool submitted = lane_submit_ring(lane, old_idx);
    (void)submitted; // best-effort; if queue full, drop and let drain catch up
    return true;
}

RingBufferHeader* ring_pool_get_active_header(RingPool* pool) {
    if (!pool) return nullptr;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    Lane* lane = (p->lane_type == 0) ? thread_lanes_get_index_lane(p->lanes)
                                     : thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return nullptr;
    return thread_registry_get_active_ring_header(p->reg, lane);
}

bool ring_pool_handle_exhaustion(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    Lane* lane = (p->lane_type == 0) ? thread_lanes_get_index_lane(p->lanes)
                                     : thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return false;

    // Drop-oldest policy: reclaim capacity by discarding the oldest submitted ring.
    // This pops from the submit queue (oldest) and returns the ring directly to the free queue.
    // If submit queue is empty, we cannot recover capacity here.
    uint32_t oldest = lane_take_ring(lane);
    if (oldest == UINT32_MAX) {
        return false;
    }
    // Return the ring back to producer side as free.
    // This should normally succeed because we're currently out of free rings,
    // but guard against a full free queue just in case.
    return lane_return_ring(lane, oldest);
}

bool ring_pool_mark_detail(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    if (p->lane_type != 1) return true; // no-op for index lanes
    auto* cpp_lanes = ada::internal::to_cpp(p->lanes);
    // Use events_generated as a visible marker in tests; increment relaxed.
    cpp_lanes->events_generated.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ring_pool_is_detail_marked(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    if (p->lane_type != 1) return false;
    auto* cpp_lanes = ada::internal::to_cpp(p->lanes);
    return cpp_lanes->events_generated.load(std::memory_order_relaxed) != 0;
}

} // extern "C"
