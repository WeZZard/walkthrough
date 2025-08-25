# Tech Design — M1 E1 I2 Baseline Hooks

## Objective

Implement ring-pool architecture with proper IPC synchronization, then install a deterministic set of hooks that emit index events through the ring-pool system with TLS reentrancy guard.

## Part A: Ring-Pool Foundation (PREREQUISITE)

### Memory Layout - Per-Thread Architecture

```c
#define MAX_THREADS 64
#define RINGS_PER_LANE 4  // 1 active + 3 spares

typedef struct {
    // Thread registry
    _Atomic(uint32_t) thread_count;
    ThreadLaneSet thread_lanes[MAX_THREADS];
    
    // Global control flags
    _Atomic(bool) index_lane_enabled;
    _Atomic(bool) detail_lane_enabled;
    _Atomic(bool) capture_stack_snapshot;
    
    // Global metrics
    _Atomic(uint64_t) total_events_written;
    _Atomic(uint64_t) total_events_dropped;
} ControlBlock;

typedef struct {
    uint32_t thread_id;
    _Atomic(bool) active;
    
    // Per-thread index lane with ring pool
    struct {
        RingBuffer rings[RINGS_PER_LANE];
        _Atomic(uint32_t) active_idx;
        
        // Per-thread SPSC queues (thread → drain)
        _Atomic(uint32_t) submit_head, submit_tail;
        uint32_t submit_q[RINGS_PER_LANE];
        _Atomic(uint32_t) free_head, free_tail;
        uint32_t free_q[RINGS_PER_LANE];
        
        // Per-thread metrics
        _Atomic(uint64_t) events_written;
        _Atomic(uint64_t) events_dropped;
        _Atomic(uint32_t) pool_exhaustion_count;
    } index_lane;
    
    // Per-thread detail lane with ring pool
    struct {
        RingBuffer rings[2];  // Fewer spares needed
        _Atomic(uint32_t) active_idx;
        
        // Per-thread SPSC queues
        _Atomic(uint32_t) submit_head, submit_tail;
        uint32_t submit_q[2];
        _Atomic(uint32_t) free_head, free_tail;
        uint32_t free_q[2];
        
        // Marking flag for this thread
        _Atomic(bool) marked_event_seen;
        
        // Per-thread metrics
        _Atomic(uint64_t) events_written;
        _Atomic(uint64_t) events_dropped;
        _Atomic(uint32_t) pool_exhaustion_count;
    } detail_lane;
} ThreadLaneSet;
```

### Synchronization Protocol

#### Thread Registration and Lane Access

```c
// Thread-local storage for lane access
static __thread ThreadLaneSet* my_lanes = NULL;

ThreadLaneSet* get_thread_lanes(ControlBlock* cb) {
    if (!my_lanes) {
        // First event from this thread - register
        uint32_t idx = atomic_fetch_add(&cb->thread_count, 1);
        if (idx >= MAX_THREADS) {
            // Too many threads - fail gracefully
            return NULL;
        }
        
        my_lanes = &cb->thread_lanes[idx];
        my_lanes->thread_id = get_thread_id();
        atomic_store(&my_lanes->active, true);
        
        // Initialize ring pools for this thread
        initialize_thread_rings(my_lanes);
    }
    return my_lanes;
}
```

#### Agent-Side Ring Management (Per-Thread)

```c
// Called when current ring is full for this thread
void handle_ring_full_index(ThreadLaneSet* lanes) {
    uint32_t current = atomic_load_explicit(&lanes->index_lane.active_idx, 
                                           memory_order_acquire);
    
    // Try to get spare from this thread's free queue
    uint32_t free_head = atomic_load_explicit(&lanes->index_lane.free_head,
                                             memory_order_acquire);
    uint32_t free_tail = atomic_load_explicit(&lanes->index_lane.free_tail,
                                             memory_order_acquire);
    
    if (free_head != free_tail) {
        // Spare available in this thread's pool
        uint32_t spare_idx = lanes->index_lane.free_q[free_head % RINGS_PER_LANE];
        
        // Submit current ring for dumping
        uint32_t submit_tail = atomic_load_explicit(&lanes->index_lane.submit_tail,
                                                   memory_order_relaxed);
        lanes->index_lane.submit_q[submit_tail % RINGS_PER_LANE] = current;
        atomic_store_explicit(&lanes->index_lane.submit_tail, submit_tail + 1,
                            memory_order_release);  // RELEASE: make ring visible
        
        // Activate spare
        atomic_store_explicit(&lanes->index_lane.active_idx, spare_idx,
                            memory_order_release);  // RELEASE: activate new ring
        
        // Advance free queue head
        atomic_store_explicit(&lanes->index_lane.free_head, free_head + 1,
                            memory_order_release);
    } else {
        // No spare available - drop oldest
        atomic_fetch_add(&lanes->index_lane.pool_exhaustion_count, 1);
        RingBuffer* ring = &lanes->index_lane.rings[current];
        // Move read pointer to drop oldest event
        uint32_t new_read = (ring->write_pos + 1) % ring->capacity;
        atomic_store_explicit(&ring->read_pos, new_read, memory_order_release);
        atomic_fetch_add(&lanes->index_lane.events_dropped, 1);
    }
}
```

### Race Condition Mitigations

1. **Thread Registration Race**: Atomic increment ensures unique thread slots
2. **Per-Thread Isolation**: Each thread has its own rings - no inter-thread contention
3. **True SPSC Semantics**: One producer (thread) to one consumer (drain) per ring
4. **Ring Swap During Dump**: Controller takes atomic snapshot of ring state
5. **Queue Full/Empty Detection**: Use monotonic counters with modulo for indexing
6. **Memory Ordering**: 
   - ACQUIRE when reading shared state
   - RELEASE when publishing changes
   - RELAXED for statistics
7. **ABA Prevention**: Queue entries are indices, not pointers; monotonic counters

## Part B: Baseline Hooks (ORIGINAL GOAL)

### Hook Installation

With the ring-pool foundation in place, install hooks that emit events:

- Hook the following functions in the main module:
  - test_cli functions: `fibonacci`, `process_file`, `calculate_pi`, `recursive_function`
  - test_runloop functions: `simulate_network`, `monitor_file`, `dispatch_work`, `signal_handler`, `timer_callback`

### Event Emission Through Per-Thread Ring-Pool

```c
static void on_enter(GumInvocationContext* ic, gpointer user_data) {
    HookData* hook = (HookData*)user_data;
    ThreadLocalData* tls = get_thread_local();
    
    // Reentrancy guard
    if (tls->in_handler) return;
    tls->in_handler = true;
    
    // Get this thread's lane set
    ThreadLaneSet* lanes = get_thread_lanes(hook->control_block);
    if (!lanes) return;  // Thread limit exceeded
    
    // Get active ring for this thread
    uint32_t active_idx = atomic_load(&lanes->index_lane.active_idx);
    RingBuffer* ring = &lanes->index_lane.rings[active_idx];
    
    // Prepare event
    IndexEvent event = {
        .timestamp = platform_get_timestamp(),
        .function_id = hook->function_id,
        .thread_id = tls->thread_id,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = tls->call_depth++
    };
    
    // Write to this thread's ring (no contention with other threads)
    if (!ring_buffer_write(ring, &event)) {
        // This thread's ring is full - trigger swap
        handle_ring_full_index(lanes);
        // Retry with new ring
        active_idx = atomic_load(&lanes->index_lane.active_idx);
        ring = &lanes->index_lane.rings[active_idx];
        ring_buffer_write(ring, &event);
    }
    
    atomic_fetch_add(&lanes->index_lane.events_written, 1);
    tls->in_handler = false;
}
```

- TLS for reentrancy guard and call depth
- Each thread emits to its own ring pool (no contention)
- Handle ring full condition via per-thread swap protocol
- True SPSC semantics maintained

### Shared Memory

- Open SHM via `shared_memory_open_unique(role, pid, session_id)` using ids provided by loader
- Map control block and attach to existing ring pools
- Fail fast if ids mismatch or SHM open fails; surface clear error via controller

## References

- docs/specs/TRACER_SPEC.md (§7.1, §8.1 - Ring-pool semantics)
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md
- docs/specs/ARCHITECTURE.md (Two-lane architecture)

## Out of Scope

- Full Protobuf encoding (using binary format for M1)
- Dynamic module tracking; symbol table scanning; key-symbol lane
- Drain thread implementation (that's M1_E2_I1)
