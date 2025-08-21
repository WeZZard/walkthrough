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

#endif // TRACER_TYPES_H