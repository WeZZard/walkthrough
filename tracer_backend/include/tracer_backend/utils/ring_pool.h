#ifndef ADA_RING_POOL_H
#define ADA_RING_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque ring pool wrapper bound to a lane (index or detail)
typedef struct AdaRingPool RingPool;

// Create a ring pool for a lane in a given thread's lanes.
// lane_type: 0 = index lane, 1 = detail lane
RingPool* ring_pool_create(ThreadRegistry* registry, ThreadLaneSet* lanes, int lane_type);

// Destroy a ring pool wrapper
void ring_pool_destroy(RingPool* pool);

// Atomically swap out the active ring and submit it for draining.
// On success, stores the old ring index into out_old_idx if non-NULL.
// Returns true on success, false when no free ring is available (pool exhaustion).
bool ring_pool_swap_active(RingPool* pool, uint32_t* out_old_idx);

// Get the header of the currently active ring for this pool.
// Returns NULL on error.
RingBufferHeader* ring_pool_get_active_header(RingPool* pool);

// Pool exhaustion handling: attempt to recover capacity.
// Strategy: NOP for now (return false) as the drain must return rings.
// Returns true if capacity has been recovered and a subsequent swap may succeed.
bool ring_pool_handle_exhaustion(RingPool* pool);

// Detail lane marking helpers (for observability/testing):
// Mark the detail lane as having a trigger; for index lane this is a no-op.
bool ring_pool_mark_detail(RingPool* pool);
bool ring_pool_is_detail_marked(RingPool* pool);

#ifdef __cplusplus
}
#endif

#endif // ADA_RING_POOL_H

