#include <tracer_backend/utils/thread_pools.h>
#include <new>

struct AdaThreadPools {
    ThreadRegistry* registry;
    ThreadLaneSet* lanes;
    RingPool* index_pool;
    RingPool* detail_pool;
};

extern "C" {

ThreadPools* thread_pools_create(ThreadRegistry* registry, ThreadLaneSet* lanes) {
    if (!registry || !lanes) return nullptr;
    auto* tp = new (std::nothrow) AdaThreadPools();
    if (!tp) return nullptr;
    tp->registry = registry;
    tp->lanes = lanes;
    tp->index_pool = ring_pool_create(registry, lanes, 0);
    tp->detail_pool = ring_pool_create(registry, lanes, 1);
    if (!tp->index_pool || !tp->detail_pool) {
        if (tp->index_pool) ring_pool_destroy(tp->index_pool);
        if (tp->detail_pool) ring_pool_destroy(tp->detail_pool);
        delete tp;
        return nullptr;
    }
    return reinterpret_cast<ThreadPools*>(tp);
}

void thread_pools_destroy(ThreadPools* pools) {
    if (!pools) return;
    auto* tp = reinterpret_cast<AdaThreadPools*>(pools);
    ring_pool_destroy(tp->index_pool);
    ring_pool_destroy(tp->detail_pool);
    delete tp;
}

RingPool* thread_pools_get_index_pool(ThreadPools* pools) {
    if (!pools) return nullptr;
    auto* tp = reinterpret_cast<AdaThreadPools*>(pools);
    return tp->index_pool;
}

RingPool* thread_pools_get_detail_pool(ThreadPools* pools) {
    if (!pools) return nullptr;
    auto* tp = reinterpret_cast<AdaThreadPools*>(pools);
    return tp->detail_pool;
}

} // extern "C"

