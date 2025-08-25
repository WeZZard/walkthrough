# Tech Design — M1 E2 I1 Drain and Persist

## Objective
Implement controller-side drain thread that consumes rings from per-thread pools (already implemented in M1_E1_I2) and persists snapshots to durable files.

## Prerequisites
- Per-thread ring-pool architecture already exists (M1_E1_I2)
- Thread registry with per-thread lanes operational
- Per-thread SPSC queues functional
- Agent-side per-thread ring swapping working

## Design

### Controller-Side Drain Thread

```c
typedef struct {
    pthread_t drain_thread;
    ControlBlock* control_block;
    int index_fd;
    int detail_fd;
    char* output_dir;
    _Atomic(bool) shutdown_requested;
    
    // Metrics
    uint64_t index_dumps_total;
    uint64_t detail_dumps_total;
    uint64_t index_bytes_written;
    uint64_t detail_bytes_written;
} DrainContext;
```

### Drain Thread Main Loop (Per-Thread)

```c
void* drain_thread_main(void* arg) {
    DrainContext* ctx = (DrainContext*)arg;
    
    while (!atomic_load(&ctx->shutdown_requested)) {
        // Drain all registered threads
        uint32_t thread_count = atomic_load(&ctx->control_block->thread_count);
        
        for (uint32_t i = 0; i < thread_count; i++) {
            ThreadLaneSet* tls = &ctx->control_block->thread_lanes[i];
            
            if (!atomic_load(&tls->active)) continue;
            
            // Drain this thread's index lane
            drain_thread_lane(&tls->index_lane, 
                            ctx->index_fd,
                            tls->thread_id,
                            &ctx->index_dumps_total,
                            &ctx->index_bytes_written);
            
            // Drain this thread's detail lane (if marked)
            if (atomic_load(&tls->detail_lane.marked_event_seen)) {
                drain_thread_lane(&tls->detail_lane,
                                ctx->detail_fd,
                                tls->thread_id,
                                &ctx->detail_dumps_total,
                                &ctx->detail_bytes_written);
            }
        }
        
        // Brief sleep if no work across all threads
        if (no_pending_rings_any_thread(ctx->control_block)) {
            usleep(1000); // 1ms
        }
    }
    
    // Final drain on shutdown
    drain_all_threads_pending(ctx);
    return NULL;
}
```

### Synchronization Protocol (Per-Thread Lanes)

```c
void drain_thread_lane(Lane* lane, int fd, uint32_t thread_id,
                      uint64_t* dumps, uint64_t* bytes) {
    uint32_t head = atomic_load_explicit(&lane->submit_head, 
                                        memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&lane->submit_tail,
                                        memory_order_acquire);
    
    while (head != tail) {
        uint32_t ring_idx = lane->submit_q[head % RINGS_PER_LANE];
        
        // Snapshot this thread's ring state atomically
        RingBuffer* ring = &lane->rings[ring_idx];
        RingSnapshot snap = {
            .thread_id = thread_id,  // Track source thread
            .write_pos = atomic_load(&ring->write_pos),
            .read_pos = atomic_load(&ring->read_pos),
            .buffer = ring->buffer,
            .capacity = ring->capacity,
            .event_size = ring->event_size
        };
        
        // Write to file with thread ID preserved
        size_t written = write_ring_to_file(fd, &snap);
        *bytes += written;
        (*dumps)++;
        
        // fsync for durability before returning ring
        fsync(fd);
        
        // Return ring to this thread's free pool
        uint32_t free_tail = atomic_load_explicit(&lane->free_tail,
                                                 memory_order_relaxed);
        lane->free_q[free_tail % RINGS_PER_LANE] = ring_idx;
        atomic_store_explicit(&lane->free_tail, free_tail + 1,
                            memory_order_release);
        
        // Advance this thread's submit queue
        atomic_store_explicit(&lane->submit_head, head + 1,
                            memory_order_release);
        head++;
        
        // Clear marking flag if detail lane
        if (lane == &((ThreadLaneSet*)((char*)lane - 
                      offsetof(ThreadLaneSet, detail_lane)))->detail_lane) {
            atomic_store(&lane->marked_event_seen, false);
        }
    }
}
```

### Memory Ordering Requirements (Per-Thread)

1. **Per-Thread Submit Queue (Thread → Controller)**
   - Thread: RELEASE when incrementing tail (publishing ring)
   - Controller: ACQUIRE when reading tail (consuming ring)
   - No contention - true SPSC per thread

2. **Per-Thread Free Queue (Controller → Thread)**
   - Controller: RELEASE when incrementing tail (returning ring)
   - Thread: ACQUIRE when reading tail (reusing ring)
   - Isolated per thread - no cross-thread access

3. **Ring Snapshot**
   - Take atomic snapshot of pointers before reading buffer
   - Never modify thread-side ring state
   - Thread ID preserved in events

### File Formats

#### Index File Header (32 bytes)
```c
typedef struct {
    char magic[8];        // "ADAIDX1\0"
    uint32_t version;     // 1
    uint32_t record_size; // sizeof(IndexEvent)
    uint32_t pid;         // Process ID
    uint32_t session_id;  // Session identifier
    uint8_t reserved[8];  // Future use
} IndexFileHeader;
```

#### Detail File Format
```c
// Per-dump header
typedef struct {
    uint32_t dump_size;   // Size of this dump
    uint64_t timestamp;   // When dumped
    uint32_t event_count; // Number of events
} DetailDumpHeader;
// Followed by raw DetailEvent records
```

### Write Operations

```c
size_t write_ring_to_file(int fd, RingSnapshot* snap) {
    size_t total = 0;
    
    // Handle wraparound correctly
    uint32_t read = snap->read_pos % snap->capacity;
    uint32_t write = snap->write_pos % snap->capacity;
    
    if (write >= read) {
        // Contiguous
        size_t count = write - read;
        size_t bytes = count * snap->event_size;
        total += write(fd, snap->buffer + read * snap->event_size, bytes);
    } else {
        // Wrapped - write in two parts
        // Part 1: read to end
        size_t part1_count = snap->capacity - read;
        size_t part1_bytes = part1_count * snap->event_size;
        total += write(fd, snap->buffer + read * snap->event_size, part1_bytes);
        
        // Part 2: start to write
        size_t part2_bytes = write * snap->event_size;
        total += write(fd, snap->buffer, part2_bytes);
    }
    
    return total;
}
```

### Race Condition Handling (Per-Thread)

1. **Thread Registration During Drain**: Atomic thread_count ensures new threads visible
2. **Ring in Transit**: Snapshot taken before writing ensures consistency
3. **Per-Thread Queue Isolation**: No races between different threads' queues
4. **Queue Wraparound**: Modulo indexing with monotonic counters per thread
5. **Shutdown During Drain**: Final drain of all threads ensures no data loss
6. **Slow I/O**: Per-thread rings accumulate independently, each thread handles own exhaustion

## Out of Scope
- Protobuf encoding (binary format for M1)
- Manifest files
- Global indexes
- Per-thread ring-pool implementation (already done in M1_E1_I2)
- Thread lifecycle management (handled by agent)

## References
- docs/specs/TRACER_SPEC.md (§7.1 - Drain semantics)
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md
- M1_E1_I2 (Ring-pool implementation)