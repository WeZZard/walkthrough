#ifndef TRACER_BACKEND_H
#define TRACER_BACKEND_H

/**
 * Minimal Umbrella Header for Tracer Backend
 * 
 * This header provides the minimal interface needed for external consumers (Rust)
 * to interact with the tracer backend. It exposes only:
 * - Synchronization primitives (for shared memory)
 * - Opaque handles
 * - Control API functions
 * 
 * Internal implementation details and full type definitions are NOT included.
 * C++ components should include individual headers from utils/ directly.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Minimal Shared Types (for cross-language synchronization)
// ============================================================================

/**
 * Ring buffer header - minimal fields needed for synchronization
 * These fields are accessed atomically from both C++ and Rust
 * See docs/technical_insights/ada/ATOMIC_OPERATIONS_CROSS_LANGUAGE.md
 * 
 * Note: This matches the definition in tracer_types.h but is redefined
 * here to keep the umbrella header self-contained.
 */
#ifndef RING_BUFFER_HEADER_DEFINED
#define RING_BUFFER_HEADER_DEFINED
typedef struct {
    uint32_t magic;         // 0xADA0 for validation
    uint32_t version;       // Format version
    uint32_t capacity;      // Number of events
    uint32_t write_pos;     // Atomic: use __atomic_* ops
    uint32_t read_pos;      // Atomic: use __atomic_* ops
    uint32_t _reserved[11]; // Reserved for future use
} RingBufferHeader;
#endif

// ============================================================================
// Opaque Handles
// ============================================================================

/**
 * Opaque handle to a tracer session
 * Implementation details are hidden in C++
 */
typedef void* TracerHandle;

/**
 * Opaque handle to a drain iterator
 * Used for consuming events from ring buffers
 */
typedef void* DrainHandle;

// ============================================================================
// Tracer Control API (for Rust)
// ============================================================================

/**
 * Create a new tracer instance
 * @param output_dir Directory for trace output files
 * @return Tracer handle, or NULL on failure
 */
TracerHandle* tracer_create(const char* output_dir);

/**
 * Destroy a tracer instance
 * @param tracer Tracer handle to destroy
 */
void tracer_destroy(TracerHandle* tracer);

/**
 * Spawn a new process in suspended state
 * @param tracer Tracer handle
 * @param path Path to executable
 * @param argv Arguments (NULL-terminated array)
 * @param out_pid Output parameter for process ID
 * @return 0 on success, -1 on failure
 */
int tracer_spawn(TracerHandle* tracer, const char* path, 
                 char* const argv[], uint32_t* out_pid);

/**
 * Attach to an existing process
 * @param tracer Tracer handle
 * @param pid Process ID to attach to
 * @return 0 on success, -1 on failure
 */
int tracer_attach(TracerHandle* tracer, uint32_t pid);

/**
 * Detach from current process
 * @param tracer Tracer handle
 * @return 0 on success, -1 on failure
 */
int tracer_detach(TracerHandle* tracer);

/**
 * Resume a suspended process
 * @param tracer Tracer handle
 * @return 0 on success, -1 on failure
 */
int tracer_resume(TracerHandle* tracer);

/**
 * Install hooks in the target process
 * @param tracer Tracer handle
 * @return 0 on success, -1 on failure
 */
int tracer_install_hooks(TracerHandle* tracer);

// ============================================================================
// Event Draining API (for Rust persistence)
// ============================================================================

/**
 * Create a drain handle for consuming events
 * @param tracer Tracer handle
 * @return Drain handle, or NULL on failure
 */
DrainHandle* tracer_create_drain(TracerHandle* tracer);

/**
 * Drain events into a buffer (as raw bytes)
 * @param drain Drain handle
 * @param buffer Output buffer for events
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, 0 if no events
 */
size_t tracer_drain_events(DrainHandle* drain, uint8_t* buffer, size_t buffer_size);

/**
 * Serialize events for persistence (converts to stable format)
 * @param buffer Raw event buffer from drain
 * @param size Size of raw events
 * @param output Serialized output buffer
 * @param output_size Size of output buffer
 * @return Size of serialized data, 0 on error
 */
size_t tracer_serialize_events(const uint8_t* buffer, size_t size,
                               uint8_t* output, size_t output_size);

/**
 * Destroy a drain handle
 * @param drain Drain handle to destroy
 */
void tracer_destroy_drain(DrainHandle* drain);

// ============================================================================
// Shared Memory Access (for Rust if needed)
// ============================================================================

/**
 * Get pointer to ring buffer header in shared memory
 * @param tracer Tracer handle
 * @param lane_type 0=index, 1=detail
 * @return Pointer to RingBufferHeader, or NULL
 */
RingBufferHeader* tracer_get_ring_buffer_header(TracerHandle* tracer, int lane_type);

/**
 * Get total size of ring buffer including header
 * @param tracer Tracer handle
 * @param lane_type 0=index, 1=detail
 * @return Total size in bytes, or 0
 */
size_t tracer_get_ring_buffer_size(TracerHandle* tracer, int lane_type);

// ============================================================================
// Statistics API
// ============================================================================

#ifndef TRACER_STATS_DEFINED
#define TRACER_STATS_DEFINED
typedef struct {
    uint64_t events_captured;
    uint64_t events_dropped;
    uint64_t bytes_written;
    uint32_t active_threads;
    uint32_t hooks_installed;
} TracerStats;
#endif

/**
 * Get tracer statistics
 * @param tracer Tracer handle
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
int tracer_get_stats(TracerHandle* tracer, TracerStats* stats);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_H