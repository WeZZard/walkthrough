#ifndef THREAD_REGISTRY_INTERFACE_H
#define THREAD_REGISTRY_INTERFACE_H

/**
 * ThreadRegistry Interface Definition
 * 
 * This file defines the COMPLETE interface contract for ThreadRegistry.
 * All implementations MUST compile against this interface.
 * 
 * Design Principles:
 * - Opaque types with C API for FFI compatibility
 * - Lock-free SPSC queues per thread
 * - Dual-lane architecture (index + detail)
 * - <1μs registration latency requirement
 * 
 * Memory Layout Contract:
 * - Registry header: 64-byte aligned
 * - Thread slots: 128-byte aligned for cache isolation
 * - Ring buffers: Page-aligned for mmap
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Performance Contracts (enforced by benchmarks)
// ============================================================================

#define THREAD_REGISTRY_MAX_THREADS 64
#define THREAD_REGISTRATION_MAX_NS 1000  // <1μs requirement
#define LANE_ACCESS_MAX_NS 10            // <10ns fast path

// ============================================================================
// Opaque Type Handles (implementation hidden)
// ============================================================================

typedef struct ThreadRegistry ThreadRegistry;
typedef struct ThreadLaneSet ThreadLaneSet;
typedef struct Lane Lane;
typedef struct RingBuffer RingBuffer;

// ============================================================================
// Synchronization Primitives (shared with Rust via atomics)
// ============================================================================

/**
 * SPSC Queue Header - Lock-free single producer, single consumer
 * These fields are accessed atomically across language boundaries
 * 
 * CRITICAL: Memory ordering requirements:
 * - write_pos: memory_order_release (producer)
 * - read_pos: memory_order_acquire (consumer)
 */
typedef struct {
    uint32_t write_pos;     // Producer cache line
    uint32_t _pad1[15];     // Padding to 64 bytes
    uint32_t read_pos;      // Consumer cache line  
    uint32_t capacity;      // Immutable after init
    uint32_t _pad2[14];     // Padding to 64 bytes
} SPSCQueueHeader;

/**
 * Ring Buffer Header - Shared memory layout
 * Accessed atomically from both C++ writers and Rust readers
 */
typedef struct {
    uint32_t magic;         // 0xADA0 for validation
    uint32_t version;       // Format version (1)
    uint32_t capacity;      // Number of events
    uint32_t write_pos;     // Atomic: next write position
    uint32_t read_pos;      // Atomic: next read position
    uint32_t flags;         // Status flags
    uint32_t _reserved[10]; // Future expansion
} RingBufferHeader;

#define RING_BUFFER_MAGIC 0xADA0
#define RING_BUFFER_VERSION 1

// ============================================================================
// Thread Registry Lifecycle API
// ============================================================================

/**
 * Initialize thread registry in shared memory
 * 
 * @param memory Shared memory region (must be zeroed)
 * @param size Total size available
 * @return Registry handle or NULL on failure
 * 
 * Performance: O(1) - just header initialization
 * Thread-safe: No (single initialization)
 */
ThreadRegistry* thread_registry_init(void* memory, size_t size);

/**
 * Attach to existing thread registry
 * 
 * @param memory Pointer to existing registry
 * @return Registry handle or NULL if invalid
 * 
 * Performance: O(1) - magic number validation only
 * Thread-safe: Yes (read-only validation)
 */
ThreadRegistry* thread_registry_attach(void* memory);

/**
 * Destroy thread registry (testing only)
 * 
 * @param registry Registry to destroy
 * 
 * Performance: O(N) where N = thread count
 * Thread-safe: No (exclusive access required)
 */
void thread_registry_destroy(ThreadRegistry* registry);

// ============================================================================
// Thread Registration API (Fast Path)
// ============================================================================

/**
 * Register current thread and get lanes
 * 
 * @param registry Thread registry
 * @param thread_id System thread ID
 * @return Lane set for this thread or NULL if full
 * 
 * Performance: <1μs (enforced by benchmark)
 * Thread-safe: Yes (atomic slot allocation)
 * Memory order: acq_rel for slot allocation
 */
ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, 
                                       uintptr_t thread_id);

/**
 * Get lanes for current thread (TLS cached)
 * 
 * @return Cached lane set or NULL
 * 
 * Performance: <10ns (TLS access only)
 * Thread-safe: Yes (TLS)
 * Inline: Always (defined in header)
 */
static inline ThreadLaneSet* thread_registry_get_lanes(void);

/**
 * Unregister current thread
 * 
 * @param lanes Thread's lane set
 * 
 * Performance: O(1)
 * Thread-safe: Yes (atomic flag update)
 * Memory order: release
 */
void thread_registry_unregister(ThreadLaneSet* lanes);

// ============================================================================
// Lane Operations API
// ============================================================================

/**
 * Get index lane from thread lanes
 * 
 * @param lanes Thread lane set
 * @return Index lane (always-on events)
 * 
 * Performance: O(1) field access
 * Thread-safe: Yes (immutable after registration)
 */
Lane* thread_lanes_get_index_lane(ThreadLaneSet* lanes);

/**
 * Get detail lane from thread lanes
 * 
 * @param lanes Thread lane set  
 * @return Detail lane (selective events)
 * 
 * Performance: O(1) field access
 * Thread-safe: Yes (immutable after registration)
 */
Lane* thread_lanes_get_detail_lane(ThreadLaneSet* lanes);

/**
 * Get active ring buffer for writing
 * 
 * @param lane Lane to get ring from
 * @return Active ring buffer
 * 
 * Performance: O(1) field access
 * Thread-safe: No (single producer)
 */
RingBuffer* lane_get_active_ring(Lane* lane);

/**
 * Submit full ring for draining
 * 
 * @param lane Lane with full ring
 * @param ring_idx Ring index to submit
 * @return true if submitted, false if queue full
 * 
 * Performance: O(1) atomic write
 * Thread-safe: Yes (SPSC queue)
 * Memory order: release
 */
bool lane_submit_ring(Lane* lane, uint32_t ring_idx);

/**
 * Get free ring for new events
 * 
 * @param lane Lane needing ring
 * @return Ring index or UINT32_MAX if none
 * 
 * Performance: O(1) atomic read
 * Thread-safe: Yes (SPSC queue)
 * Memory order: acquire
 */
uint32_t lane_get_free_ring(Lane* lane);

// ============================================================================
// Drain Thread API
// ============================================================================

/**
 * Get count of active threads
 * 
 * @param registry Thread registry
 * @return Number of active threads
 * 
 * Performance: O(N) where N = max threads
 * Thread-safe: Yes (atomic reads)
 */
uint32_t thread_registry_get_active_count(ThreadRegistry* registry);

/**
 * Get thread at index for iteration
 * 
 * @param registry Thread registry
 * @param index Slot index [0, max_threads)
 * @return Thread lanes or NULL if inactive
 * 
 * Performance: O(1) array access
 * Thread-safe: Yes (atomic flag check)
 */
ThreadLaneSet* thread_registry_get_thread_at(ThreadRegistry* registry, 
                                            uint32_t index);

/**
 * Take submitted ring for draining
 * 
 * @param lane Lane to drain from
 * @return Ring index or UINT32_MAX if empty
 * 
 * Performance: O(1) atomic read
 * Thread-safe: Yes (SPSC queue)
 * Memory order: acquire
 */
uint32_t lane_take_ring(Lane* lane);

/**
 * Return drained ring to free pool
 * 
 * @param lane Lane to return to
 * @param ring_idx Ring index to return
 * @return true if returned, false if queue full
 * 
 * Performance: O(1) atomic write
 * Thread-safe: Yes (SPSC queue)
 * Memory order: release
 */
bool lane_return_ring(Lane* lane, uint32_t ring_idx);

// ============================================================================
// Statistics API
// ============================================================================

typedef struct {
    uint64_t events_captured;
    uint64_t events_dropped;
    uint64_t bytes_written;
    uint32_t active_threads;
    uint32_t hooks_installed;
} TracerStats;

/**
 * Get registry statistics
 * 
 * @param registry Thread registry
 * @param stats Output statistics
 * 
 * Performance: O(N) where N = thread count
 * Thread-safe: Yes (atomic reads)
 */
void thread_registry_get_stats(ThreadRegistry* registry, TracerStats* stats);

/**
 * Dump registry state for debugging
 * 
 * @param registry Thread registry
 * 
 * Performance: O(N) + I/O
 * Thread-safe: Yes (read-only snapshot)
 */
void thread_registry_dump(ThreadRegistry* registry);

// ============================================================================
// TLS Implementation (must be in header for inline)
// ============================================================================

extern __thread ThreadLaneSet* tls_my_lanes;

static inline ThreadLaneSet* thread_registry_get_lanes(void) {
    return tls_my_lanes;  // <10ns access
}

#ifdef __cplusplus
}
#endif

#endif // THREAD_REGISTRY_INTERFACE_H