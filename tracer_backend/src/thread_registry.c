#include "thread_registry.h"
#include "ring_buffer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>

// ============================================================================
// Thread-local storage for fast path
// ============================================================================

__thread ThreadLaneSet* tls_my_lanes = NULL;

// ============================================================================
// Internal helpers
// ============================================================================

// Calculate memory requirements for a single lane
static size_t calculate_lane_memory_size(uint32_t ring_count, size_t ring_size, 
                                         size_t queue_size) {
    size_t total = 0;
    // Array of RingBuffer pointers
    total += ring_count * sizeof(struct RingBuffer*);
    // Ring buffers themselves
    total += ring_count * ring_size;
    // Submit and free queues (each holds uint32_t indices)
    total += 2 * queue_size * sizeof(uint32_t);
    return total;
}

// Calculate total memory requirements for the registry
static size_t calculate_registry_memory_size(void) {
    size_t total = sizeof(ThreadRegistry);
    
    // For each potential thread slot
    size_t per_thread = 0;
    
    // Index lane: 4 rings of 64KB each, queues of size 8
    per_thread += calculate_lane_memory_size(RINGS_PER_INDEX_LANE, 
                                            64 * 1024,  // 64KB per ring
                                            8);         // Queue size
    
    // Detail lane: 2 rings of 256KB each, queues of size 4  
    per_thread += calculate_lane_memory_size(RINGS_PER_DETAIL_LANE,
                                            256 * 1024, // 256KB per ring
                                            4);         // Queue size
    
    total += MAX_THREADS * per_thread;
    return total;
}

// ============================================================================
// Thread Registry initialization
// ============================================================================

ThreadRegistry* thread_registry_init(void* memory, size_t size) {
    if (!memory || size < sizeof(ThreadRegistry)) {
        return NULL;
    }
    
    // Calculate required size
    size_t required = calculate_registry_memory_size();
    if (size < required) {
        fprintf(stderr, "thread_registry_init: insufficient memory, need %zu, got %zu\n",
                required, size);
        return NULL;
    }
    
    // Clear all memory first
    memset(memory, 0, size);
    
    ThreadRegistry* registry = (ThreadRegistry*)memory;
    
    // Initialize atomic fields
    atomic_init(&registry->thread_count, 0);
    atomic_init(&registry->accepting_registrations, true);
    atomic_init(&registry->shutdown_requested, false);
    
    // Initialize all thread slots as inactive
    for (int i = 0; i < MAX_THREADS; i++) {
        ThreadLaneSet* tls = &registry->thread_lanes[i];
        atomic_init(&tls->active, false);
        tls->slot_index = i;
        
        // Initialize atomics in lanes
        atomic_init(&tls->index_lane.active_idx, 0);
        atomic_init(&tls->index_lane.submit_head, 0);
        atomic_init(&tls->index_lane.submit_tail, 0);
        atomic_init(&tls->index_lane.free_head, 0);
        atomic_init(&tls->index_lane.free_tail, 0);
        atomic_init(&tls->index_lane.marked_event_seen, false);
        atomic_init(&tls->index_lane.events_written, 0);
        atomic_init(&tls->index_lane.events_dropped, 0);
        atomic_init(&tls->index_lane.ring_swaps, 0);
        atomic_init(&tls->index_lane.pool_exhaustions, 0);
        
        atomic_init(&tls->detail_lane.active_idx, 0);
        atomic_init(&tls->detail_lane.submit_head, 0);
        atomic_init(&tls->detail_lane.submit_tail, 0);
        atomic_init(&tls->detail_lane.free_head, 0);
        atomic_init(&tls->detail_lane.free_tail, 0);
        atomic_init(&tls->detail_lane.marked_event_seen, false);
        atomic_init(&tls->detail_lane.events_written, 0);
        atomic_init(&tls->detail_lane.events_dropped, 0);
        atomic_init(&tls->detail_lane.ring_swaps, 0);
        atomic_init(&tls->detail_lane.pool_exhaustions, 0);
        
        atomic_init(&tls->events_generated, 0);
        atomic_init(&tls->last_event_timestamp, 0);
    }
    
    // Memory layout after ThreadRegistry structure
    uint8_t* current = (uint8_t*)memory + sizeof(ThreadRegistry);
    
    // Allocate memory for each thread's lanes
    for (int i = 0; i < MAX_THREADS; i++) {
        ThreadLaneSet* tls = &registry->thread_lanes[i];
        
        // Index lane setup (4 rings of 64KB each)
        size_t index_ring_size = 64 * 1024;
        tls->index_lane.ring_count = RINGS_PER_INDEX_LANE;
        
        // Allocate array of RingBuffer pointers
        tls->index_lane.rings = (struct RingBuffer**)current;
        current += RINGS_PER_INDEX_LANE * sizeof(struct RingBuffer*);
        
        // Allocate and initialize each ring buffer
        for (uint32_t j = 0; j < RINGS_PER_INDEX_LANE; j++) {
            tls->index_lane.rings[j] = ring_buffer_create(current, index_ring_size, sizeof(IndexEvent));
            current += index_ring_size;
        }
        
        // Index lane SPSC queues (8 entries each)
        tls->index_lane.submit_queue_size = 8;
        tls->index_lane.submit_queue = (uint32_t*)current;
        current += 8 * sizeof(uint32_t);
        
        tls->index_lane.free_queue_size = 8;
        tls->index_lane.free_queue = (uint32_t*)current;
        current += 8 * sizeof(uint32_t);
        
        // Detail lane setup (2 rings of 256KB each)
        size_t detail_ring_size = 256 * 1024;
        tls->detail_lane.ring_count = RINGS_PER_DETAIL_LANE;
        
        // Allocate array of RingBuffer pointers
        tls->detail_lane.rings = (struct RingBuffer**)current;
        current += RINGS_PER_DETAIL_LANE * sizeof(struct RingBuffer*);
        
        // Allocate and initialize each ring buffer
        for (uint32_t j = 0; j < RINGS_PER_DETAIL_LANE; j++) {
            tls->detail_lane.rings[j] = ring_buffer_create(current, detail_ring_size, sizeof(DetailEvent));
            current += detail_ring_size;
        }
        
        // Detail lane SPSC queues (4 entries each)
        tls->detail_lane.submit_queue_size = 4;
        tls->detail_lane.submit_queue = (uint32_t*)current;
        current += 4 * sizeof(uint32_t);
        
        tls->detail_lane.free_queue_size = 4;
        tls->detail_lane.free_queue = (uint32_t*)current;
        current += 4 * sizeof(uint32_t);
    }
    
    return registry;
}

ThreadRegistry* thread_registry_attach(void* memory) {
    if (!memory) {
        return NULL;
    }
    
    ThreadRegistry* registry = (ThreadRegistry*)memory;
    
    // Basic validation - check magic values or version if we had them
    // For now, just check that thread_count is reasonable
    uint32_t count = atomic_load_explicit(&registry->thread_count, memory_order_acquire);
    if (count > MAX_THREADS) {
        return NULL;
    }
    
    return registry;
}

// ============================================================================
// Thread registration
// ============================================================================

bool needs_log_thread_registry_registry = false;

#define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define THREAD_REGISTRY_LOG(fmt, ...) \
    if (needs_log_thread_registry_registry) { \
        DEBUG_LOG(fmt, ##__VA_ARGS__); \
    }

ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, uint32_t thread_id) {
    // Check if we already have a cached lane set
    if (tls_my_lanes != NULL) {
        THREAD_REGISTRY_LOG("Thread %u already registered\n", thread_id);
        return tls_my_lanes;
    }
    
    // Check if still accepting registrations
    if (!atomic_load_explicit(&registry->accepting_registrations, memory_order_acquire)) {
        THREAD_REGISTRY_LOG("Thread registry not accepting registrations\n");
        return NULL;
    }
    
    // Atomically allocate a slot
    // memory_order_acq_rel ensures:
    // - acquire: see all previous thread registrations
    // - release: make our registration visible to others
    uint32_t slot = atomic_fetch_add_explicit(&registry->thread_count, 1, 
                                              memory_order_acq_rel);
    
    if (slot >= MAX_THREADS) {
        THREAD_REGISTRY_LOG("Thread registry full\n");
        // Too many threads, roll back the count
        atomic_fetch_sub_explicit(&registry->thread_count, 1, memory_order_acq_rel);
        return NULL;
    }
    
    // Get our thread lane set
    ThreadLaneSet* lanes = &registry->thread_lanes[slot];
    
    // Initialize thread identification
    lanes->thread_id = thread_id;
    lanes->slot_index = slot;
    
    // Initialize lanes - put all rings except first into free queue
    // Index lane
    for (uint32_t i = 1; i < lanes->index_lane.ring_count; i++) {
        THREAD_REGISTRY_LOG("Thread %u: Index lane ring %u\n", thread_id, i);
        lanes->index_lane.free_queue[i - 1] = i;
    }
    atomic_store_explicit(&lanes->index_lane.free_tail, 
                         lanes->index_lane.ring_count - 1,
                         memory_order_release);
    
    // Detail lane
    for (uint32_t i = 1; i < lanes->detail_lane.ring_count; i++) {
        THREAD_REGISTRY_LOG("Thread %u: Detail lane ring %u\n", thread_id, i);
        lanes->detail_lane.free_queue[i - 1] = i;
    }
    atomic_store_explicit(&lanes->detail_lane.free_tail,
                         lanes->detail_lane.ring_count - 1,
                         memory_order_release);
    
    // Mark thread as active (release ensures initialization is visible)
    atomic_store_explicit(&lanes->active, true, memory_order_release);
    
    // Cache in TLS for fast path
    tls_my_lanes = lanes;
    
    return lanes;
}

void thread_registry_unregister(ThreadLaneSet* lanes) {
    if (!lanes) {
        return;
    }
    
    // Mark as inactive (release ensures all our writes are visible)
    atomic_store_explicit(&lanes->active, false, memory_order_release);
    
    // Clear TLS cache
    if (tls_my_lanes == lanes) {
        tls_my_lanes = NULL;
    }
}

// ============================================================================
// Lane operations - SPSC queue implementation
// ============================================================================

bool lane_init(Lane* lane, void* ring_memory, size_t ring_size,
               uint32_t ring_count, void* queue_memory, size_t queue_size) {
    if (!lane || !ring_memory || !queue_memory || ring_count == 0) {
        return false;
    }
    
    // Note: Ring buffers are already initialized in thread_registry_init
    // This function is kept for API compatibility but may not be needed
    
    // Initialize SPSC queues if not already done
    if (!lane->submit_queue) {
        uint32_t* queue_ptr = (uint32_t*)queue_memory;
        lane->submit_queue = queue_ptr;
        lane->submit_queue_size = queue_size;
        queue_ptr += queue_size;
        
        lane->free_queue = queue_ptr;
        lane->free_queue_size = queue_size;
    }
    
    // Put all rings except first into free queue
    for (uint32_t i = 1; i < ring_count; i++) {
        lane->free_queue[i - 1] = i;
    }
    atomic_store_explicit(&lane->free_tail, ring_count - 1, memory_order_release);
    
    return true;
}

bool lane_submit_ring(Lane* lane, uint32_t ring_idx) {
    // Load tail with relaxed ordering (we're the only producer)
    uint32_t tail = atomic_load_explicit(&lane->submit_tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&lane->submit_head, memory_order_acquire);
    
    // Check if queue is full
    uint32_t next_tail = (tail + 1) % lane->submit_queue_size;
    if (next_tail == head) {
        return false;  // Queue full
    }
    
    // Store ring index
    lane->submit_queue[tail] = ring_idx;
    
    // Update tail with release ordering to publish the new entry
    // This ensures the ring index write is visible before tail update
    atomic_store_explicit(&lane->submit_tail, next_tail, memory_order_release);
    
    return true;
}

uint32_t lane_take_ring(Lane* lane) {
    // Load head and tail
    uint32_t head = atomic_load_explicit(&lane->submit_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&lane->submit_tail, memory_order_acquire);
    
    // Check if queue is empty
    if (head == tail) {
        return UINT32_MAX;  // Queue empty
    }
    
    // Get ring index
    uint32_t ring_idx = lane->submit_queue[head];
    
    // Update head with release ordering
    // This ensures we've read the ring index before updating head
    uint32_t next_head = (head + 1) % lane->submit_queue_size;
    atomic_store_explicit(&lane->submit_head, next_head, memory_order_release);
    
    return ring_idx;
}

bool lane_return_ring(Lane* lane, uint32_t ring_idx) {
    // Load tail with relaxed ordering (we're the only producer for free queue from drain side)
    uint32_t tail = atomic_load_explicit(&lane->free_tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&lane->free_head, memory_order_acquire);
    
    // Check if queue is full
    uint32_t next_tail = (tail + 1) % lane->free_queue_size;
    if (next_tail == head) {
        return false;  // Queue full
    }
    
    // Store ring index
    lane->free_queue[tail] = ring_idx;
    
    // Update tail with release ordering to publish
    atomic_store_explicit(&lane->free_tail, next_tail, memory_order_release);
    
    return true;
}

uint32_t lane_get_free_ring(Lane* lane) {
    // Load head and tail
    uint32_t head = atomic_load_explicit(&lane->free_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&lane->free_tail, memory_order_acquire);
    
    // Check if queue is empty
    if (head == tail) {
        return UINT32_MAX;  // Queue empty
    }
    
    // Get ring index
    uint32_t ring_idx = lane->free_queue[head];
    
    // Update head with release ordering
    uint32_t next_head = (head + 1) % lane->free_queue_size;
    atomic_store_explicit(&lane->free_head, next_head, memory_order_release);
    
    return ring_idx;
}

// ============================================================================
// Drain thread operations
// ============================================================================

uint32_t thread_registry_get_active_count(ThreadRegistry* registry) {
    uint32_t active_count = 0;
    uint32_t total = atomic_load_explicit(&registry->thread_count, memory_order_acquire);
    
    for (uint32_t i = 0; i < total && i < MAX_THREADS; i++) {
        if (atomic_load_explicit(&registry->thread_lanes[i].active, memory_order_acquire)) {
            active_count++;
        }
    }
    
    return active_count;
}

ThreadLaneSet* thread_registry_get_thread_at(ThreadRegistry* registry, uint32_t index) {
    if (index >= MAX_THREADS) {
        return NULL;
    }
    
    uint32_t count = atomic_load_explicit(&registry->thread_count, memory_order_acquire);
    if (index >= count) {
        return NULL;
    }
    
    ThreadLaneSet* lanes = &registry->thread_lanes[index];
    
    // Check if thread is active
    if (!atomic_load_explicit(&lanes->active, memory_order_acquire)) {
        return NULL;
    }
    
    return lanes;
}

// ============================================================================
// Statistics and debugging
// ============================================================================

void thread_registry_get_stats(ThreadRegistry* registry, TracerStats* stats) {
    if (!registry || !stats) {
        return;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    uint32_t count = atomic_load_explicit(&registry->thread_count, memory_order_acquire);
    
    for (uint32_t i = 0; i < count && i < MAX_THREADS; i++) {
        ThreadLaneSet* lanes = &registry->thread_lanes[i];
        
        if (!atomic_load_explicit(&lanes->active, memory_order_acquire)) {
            continue;
        }
        
        // Aggregate stats from index lane
        stats->events_captured += atomic_load_explicit(&lanes->index_lane.events_written,
                                                       memory_order_relaxed);
        stats->events_dropped += atomic_load_explicit(&lanes->index_lane.events_dropped,
                                                      memory_order_relaxed);
        
        // Aggregate stats from detail lane
        stats->events_captured += atomic_load_explicit(&lanes->detail_lane.events_written,
                                                       memory_order_relaxed);
        stats->events_dropped += atomic_load_explicit(&lanes->detail_lane.events_dropped,
                                                      memory_order_relaxed);
    }
}

void thread_registry_dump(ThreadRegistry* registry) {
    if (!registry) {
        return;
    }
    
    printf("ThreadRegistry @ %p\n", (void*)registry);
    printf("  thread_count: %u\n", atomic_load_explicit(&registry->thread_count, 
                                                        memory_order_acquire));
    printf("  accepting_registrations: %s\n", 
           atomic_load_explicit(&registry->accepting_registrations, memory_order_acquire) 
           ? "true" : "false");
    printf("  shutdown_requested: %s\n",
           atomic_load_explicit(&registry->shutdown_requested, memory_order_acquire)
           ? "true" : "false");
    
    uint32_t count = atomic_load_explicit(&registry->thread_count, memory_order_acquire);
    
    for (uint32_t i = 0; i < count && i < MAX_THREADS; i++) {
        ThreadLaneSet* lanes = &registry->thread_lanes[i];
        
        if (!atomic_load_explicit(&lanes->active, memory_order_acquire)) {
            continue;
        }
        
        printf("\n  Thread[%u]:\n", i);
        printf("    thread_id: %u\n", lanes->thread_id);
        printf("    slot_index: %u\n", lanes->slot_index);
        printf("    events_generated: %llu\n", 
               (unsigned long long)atomic_load_explicit(&lanes->events_generated,
                                                        memory_order_relaxed));
        
        printf("    Index Lane:\n");
        printf("      events_written: %llu\n",
               (unsigned long long)atomic_load_explicit(&lanes->index_lane.events_written,
                                                        memory_order_relaxed));
        printf("      events_dropped: %llu\n",
               (unsigned long long)atomic_load_explicit(&lanes->index_lane.events_dropped,
                                                        memory_order_relaxed));
        printf("      ring_swaps: %u\n",
               atomic_load_explicit(&lanes->index_lane.ring_swaps, memory_order_relaxed));
        
        printf("    Detail Lane:\n");
        printf("      events_written: %llu\n",
               (unsigned long long)atomic_load_explicit(&lanes->detail_lane.events_written,
                                                        memory_order_relaxed));
        printf("      events_dropped: %llu\n",
               (unsigned long long)atomic_load_explicit(&lanes->detail_lane.events_dropped,
                                                        memory_order_relaxed));
        printf("      ring_swaps: %u\n",
               atomic_load_explicit(&lanes->detail_lane.ring_swaps, memory_order_relaxed));
    }
}