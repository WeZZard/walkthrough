#ifndef TRACER_TYPES_H
#define TRACER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// Event kinds
typedef enum {
    EVENT_KIND_CALL = 1,
    EVENT_KIND_RETURN = 2,
    EVENT_KIND_EXCEPTION = 3
} EventKind;

// Process state
typedef enum {
    PROCESS_STATE_UNINITIALIZED = 0,
    PROCESS_STATE_INITIALIZED,
    PROCESS_STATE_SPAWNING,
    PROCESS_STATE_SUSPENDED,
    PROCESS_STATE_ATTACHING,
    PROCESS_STATE_ATTACHED,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_DETACHING,
    PROCESS_STATE_FAILED
} ProcessState;

// Flight recorder state
typedef enum {
    FLIGHT_RECORDER_IDLE = 0,
    FLIGHT_RECORDER_ARMED,
    FLIGHT_RECORDER_PRE_ROLL,
    FLIGHT_RECORDER_RECORDING,
    FLIGHT_RECORDER_POST_ROLL
} FlightRecorderState;

// Compact index event (32 bytes)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;      // Monotonic timestamp
    uint64_t function_id;    // (moduleId << 32) | symbolIndex
    uint32_t thread_id;      // Thread identifier
    uint32_t event_kind;     // EventKind
    uint32_t call_depth;     // Call stack depth
    uint32_t _padding;       // Alignment padding
} IndexEvent;

// Rich detail event (512 bytes)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;
    uint64_t function_id;
    uint32_t thread_id;
    uint32_t event_kind;
    uint32_t call_depth;
    uint32_t _pad1;
    
    // ARM64 ABI registers (x0-x7 for arguments)
    uint64_t x_regs[8];
    uint64_t lr;            // Link register
    uint64_t fp;            // Frame pointer
    uint64_t sp;            // Stack pointer
    
    // Stack snapshot (128 bytes default)
    uint8_t stack_snapshot[128];
    uint32_t stack_size;
    
    // Padding to 512 bytes
    uint8_t _padding[512 - 248];
} DetailEvent;

// Ring buffer header
typedef struct {
    uint32_t magic;         // 0xADA0
    uint32_t version;       // Format version
    uint32_t capacity;      // Number of events
    _Atomic uint32_t write_pos;     // Write position (atomic)
    _Atomic uint32_t read_pos;      // Read position (atomic)
    uint32_t _reserved[11]; // Reserved for future use
} RingBufferHeader;

// Thread info for registry
typedef struct {
    uint32_t thread_id;
    uint32_t status;        // 0=inactive, 1=active
    uint64_t ring_offset;   // Offset to SPSC ring
    uint32_t ring_size;     // Size of ring buffer
    uint32_t _padding;
} ThreadInfo;

// Control block for shared state
typedef struct {
    ProcessState process_state;
    FlightRecorderState flight_state;
    uint32_t pre_roll_ms;
    uint32_t post_roll_ms;
    uint64_t trigger_time;
    uint32_t index_lane_enabled;
    uint32_t detail_lane_enabled;
    uint32_t capture_stack_snapshot;  // Enable 128-byte stack capture
    uint32_t _reserved[7];  // Reserved for future flags
} ControlBlock;

// Statistics
typedef struct {
    uint64_t events_captured;
    uint64_t events_dropped;
    uint64_t bytes_written;
    uint64_t drain_cycles;
    double cpu_overhead_percent;
    double memory_usage_mb;
} TracerStats;

// ============================================================================
// Thread Registry - Per-thread lane architecture for lock-free operation
// ============================================================================

#define MAX_THREADS 64
#define RINGS_PER_INDEX_LANE 4
#define RINGS_PER_DETAIL_LANE 2
#define CACHE_LINE_SIZE 64

// Forward declaration - RingBuffer is defined in ring_buffer.h
struct RingBuffer;

// Lane structure - manages rings for one lane (index or detail)
// Aligned to cache line to prevent false sharing
typedef struct __attribute__((aligned(CACHE_LINE_SIZE))) {
    // Ring pool for this lane
    struct RingBuffer** rings;       // Array of ring buffer pointers
    uint32_t ring_count;             // Number of rings in pool
    _Atomic(uint32_t) active_idx;   // Currently active ring index
    
    // SPSC submit queue (thread -> drain)
    _Atomic(uint32_t) submit_head;  // Consumer position (drain reads)
    _Atomic(uint32_t) submit_tail;  // Producer position (thread writes)
    uint32_t* submit_queue;          // Queue of ring indices ready to drain
    uint32_t submit_queue_size;      // Queue capacity
    
    // SPSC free queue (drain -> thread)  
    _Atomic(uint32_t) free_head;    // Consumer position (thread reads)
    _Atomic(uint32_t) free_tail;    // Producer position (drain writes)
    uint32_t* free_queue;            // Queue of empty ring indices
    uint32_t free_queue_size;        // Queue capacity
    
    // Lane-specific state
    _Atomic(bool) marked_event_seen; // For detail lane trigger detection
    
    // Lane metrics
    _Atomic(uint64_t) events_written;
    _Atomic(uint64_t) events_dropped;
    _Atomic(uint32_t) ring_swaps;
    _Atomic(uint32_t) pool_exhaustions;
} Lane;

// ThreadLaneSet - per-thread structure containing both lanes
// Aligned to cache line for optimal performance
typedef struct __attribute__((aligned(CACHE_LINE_SIZE))) {
    // Thread identification
    uint32_t thread_id;              // System thread ID
    uint32_t slot_index;             // Index in registry (0-63)
    _Atomic(bool) active;            // Thread still alive
    
    // Per-thread lanes
    Lane index_lane;                 // Index events (4 rings)
    Lane detail_lane;                // Detail events (2 rings)
    
    // Thread-local metrics
    _Atomic(uint64_t) events_generated;
    _Atomic(uint64_t) last_event_timestamp;
} ThreadLaneSet;

// ThreadRegistry - global registry of all threads
// This is the main structure allocated in shared memory
typedef struct __attribute__((aligned(CACHE_LINE_SIZE))) {
    // Global thread registry
    _Atomic(uint32_t) thread_count;       // Number of registered threads
    
    // Global control flags
    _Atomic(bool) accepting_registrations; // Still accepting new threads
    _Atomic(bool) shutdown_requested;      // Shutdown in progress
    
    // Array of thread lane sets (bulk of the structure)
    ThreadLaneSet thread_lanes[MAX_THREADS]; // Array of thread lane sets
} ThreadRegistry;

#endif // TRACER_TYPES_H