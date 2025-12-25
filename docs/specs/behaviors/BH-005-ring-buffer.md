---
id: BH-005
title: Ring Buffer Management with Lane-Specific Semantics
status: active
source: docs/specs/TRACER_SPEC.md
---

# Ring Buffer Management with Lane-Specific Semantics

## Context

**Given:**
- The tracer must handle high-throughput event streams (5M+ events/second)
- Events are captured in shared memory using ring buffers
- There are two lanes: index lane (always-on) and detail lane (selective)
- Each lane has different persistence semantics
- Ring buffers must be lock-free for performance
- Memory usage must be bounded

## Trigger

**When:** The tracer collects events from the instrumented application

## Outcome

**Then:**
- Per-thread SPSC (Single Producer Single Consumer) ring buffers are used at L1
- An MPSC (Multiple Producer Single Consumer) drain feeds from L1 to L2
- Each lane (index and detail) uses a bounded ring-pool architecture
- Ring buffers are fixed-size to bound dump time
- The pool bounds total memory usage
- A must-not-drop lane exists for crash/signal diagnostics
- Ring buffers use atomic operations for lock-free access
- Memory barriers ensure proper cross-CPU visibility

## Ring-Pool Architecture

### Index Lane (Always-On Persistence)
- Uses a bounded ring-pool: 1 active + K_index spares (typically 2-3)
- When the active ring becomes full, the agent submits the ring to the controller
- The agent atomically swaps to a spare ring
- The controller dumps the submitted ring to disk and returns it to the free pool
- If the pool is exhausted, the agent continues with drop-oldest policy
- Index lane must never block

### Detail Lane (Always-On Capture, Windowed Persistence)
- Uses a bounded ring-pool: 1 active + K_detail spares (typically 1-2)
- Dump is triggered only when BOTH conditions hold:
  1. The active detail ring is full
  2. At least one marked event has been seen since the last dump
- Marked events are determined by the current marking policy
- On trigger, the agent submits the ring and swaps to a spare
- The controller dumps the ring and returns it to the free pool
- If the pool is exhausted, the agent continues with drop-oldest policy
- Further submissions may be coalesced if triggers occur rapidly

## Control Block and Memory Model

The control block in shared memory includes for each lane:

```c
struct RingDescriptor {
    void* base_address;      // Base address of ring buffer
    size_t capacity;         // Total capacity in bytes
    size_t record_size;      // Size of each record
    uint32_t id;             // Ring identifier
};

struct LaneControl {
    RingDescriptor rings[N];              // Ring descriptors
    atomic_uint32_t active_ring_idx;      // Current active ring
    SPSCQueue submit_q;                   // Agent → Controller queue
    SPSCQueue free_q;                     // Controller → Agent queue
    atomic_bool marked_event_seen;        // Detail lane only

    // Metrics
    atomic_uint64_t dumps_total;
    atomic_uint64_t dump_bytes_last;
    atomic_uint64_t dump_ms_last;
    atomic_uint32_t pool_free_min;
    atomic_uint64_t pool_exhausted_count;
};
```

## Memory Ordering

- **Agent (Writer)**:
  - Uses release semantics when pushing to `submit_q`
  - Uses release semantics when swapping `active_ring_idx`

- **Controller (Reader)**:
  - Uses acquire semantics on `submit_q` pop
  - Uses acquire semantics prior to snapshotting ring headers/pointers

- **Event Writes**:
  - Use SPSC ring discipline
  - No OS locks on the hot path

## Capacity Planning

The system supports configuration of:
- Target peak rate (Rp) in events/second
- Burst window (Tb) in seconds

Ring buffers are pre-sized and pre-faulted accordingly:
- Event capacity K = floor(Rp × Tb / s) where s is record size
- Ring size = K × record_size
- Rings are mlock'd to avoid page faults
- Huge pages may be used where available

## Edge Cases

### Ring Exhaustion on Index Lane
**Given:** The index lane ring pool is exhausted (all spares in use)
**When:** The active ring becomes full
**Then:**
- The agent continues writing with drop-oldest policy
- Oldest events in the active ring are overwritten
- `pool_exhausted_count` metric is incremented
- The agent does not block
- When a spare becomes available, normal operation resumes

### Ring Exhaustion on Detail Lane
**Given:** The detail lane ring pool is exhausted
**When:** The active ring becomes full AND a marked event is seen
**Then:**
- The agent continues writing with drop-oldest policy
- Further submissions may be coalesced
- `pool_exhausted_count` metric is incremented
- The agent does not block
- When a spare becomes available, normal operation resumes

### Rapid Marking Events
**Given:** Marked events occur very frequently (e.g., every few milliseconds)
**When:** Multiple marked events are seen before a dump completes
**Then:**
- The detail lane coalesces submissions
- Only one dump occurs per ring-full cycle
- The `marked_event_seen` flag is reset after submission
- This prevents excessive dump operations

### Cache Alignment
**Given:** Ring buffers must minimize false sharing and cache misses
**When:** Ring buffers are allocated
**Then:**
- Records are cacheline-aligned
- Ring metadata is on separate cachelines from data
- Producer and consumer metadata are on separate cachelines
- The drain thread is pinned to a dedicated core if possible

### Pre-Faulting and Memory Locking
**Given:** Page faults during event logging would cause unacceptable latency
**When:** Ring buffers are initialized
**Then:**
- Ring buffers are pre-faulted by touching all pages
- Rings are mlock'd to prevent swapping
- On systems supporting huge pages, they are used to reduce TLB pressure
- Memory is never released until the tracing session ends

## Metrics

The following metrics are tracked for each lane:
- `dumps_total`: Total number of ring dumps
- `dump_bytes_last`: Bytes written in last dump
- `dump_ms_last`: Milliseconds taken for last dump
- `pool_free_min`: Minimum free rings observed (watermark)
- `pool_exhausted_count`: Times the pool was exhausted

Additional metrics:
- Events/second (per lane)
- Drops/second with reason codes (per lane)
- Buffer fill percentage
- Encoder latency (time from event to disk)

## References

- Original: `docs/specs/TRACER_SPEC.md` (archived source - sections 7, BP-001 to BP-004)
- Related: `BH-004-function-tracing` (Function Tracing)
- Related: `BH-006-backpressure` (Backpressure Handling)
- Related: `BH-007-selective-persistence` (Selective Persistence)
