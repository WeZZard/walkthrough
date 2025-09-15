#ifndef ADA_THREAD_POOLS_H
#define ADA_THREAD_POOLS_H

#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/ring_pool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque aggregate managing per-thread index/detail ring pools
typedef struct AdaThreadPools ThreadPools;

// Create thread pools for the given thread's lanes
ThreadPools* thread_pools_create(ThreadRegistry* registry, ThreadLaneSet* lanes);

// Destroy pools
void thread_pools_destroy(ThreadPools* pools);

// Accessors
RingPool* thread_pools_get_index_pool(ThreadPools* pools);
RingPool* thread_pools_get_detail_pool(ThreadPools* pools);

#ifdef __cplusplus
}
#endif

#endif // ADA_THREAD_POOLS_H

