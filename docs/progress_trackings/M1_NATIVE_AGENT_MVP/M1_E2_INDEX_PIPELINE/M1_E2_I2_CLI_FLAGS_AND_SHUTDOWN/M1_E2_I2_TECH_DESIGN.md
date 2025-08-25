# Tech Design — M1 E2 I2 CLI Flags and Shutdown

## Objective
Expose output path and duration flags, ensure clean shutdown with proper synchronization of the ring-pool system.

## Prerequisites
- Per-thread ring-pool architecture operational (M1_E1_I2)
- Per-thread drain implementation complete (M1_E2_I1)
- Thread registry with all active threads tracked

## Design

### CLI Interface

```bash
# Spawn mode
tracer spawn <executable> [-- args...] \
    --output <dir> \
    --duration <seconds> \
    --stack-bytes <N>

# Attach mode  
tracer attach <pid> \
    --output <dir> \
    --duration <seconds> \
    --stack-bytes <N>
```

### Shutdown Synchronization

```c
typedef struct {
    _Atomic(bool) shutdown_requested;
    _Atomic(bool) accepting_events;
    pthread_mutex_t shutdown_mutex;
    pthread_cond_t shutdown_complete;
} ShutdownState;

void initiate_shutdown(TracerContext* ctx) {
    // Phase 1: Stop accepting new events
    atomic_store_explicit(&ctx->shutdown.accepting_events, false,
                         memory_order_release);
    
    // Phase 2: Flush all threads' active rings
    flush_all_thread_rings(ctx->control_block);
    
    // Phase 3: Signal drain thread to exit
    atomic_store_explicit(&ctx->shutdown.shutdown_requested, true,
                         memory_order_release);
    
    // Phase 4: Wait for drain thread completion
    pthread_join(ctx->drain_thread, NULL);
    
    // Phase 5: Final file sync
    fsync(ctx->index_fd);
    fsync(ctx->detail_fd);
    
    // Phase 6: Cleanup
    cleanup_shared_memory(ctx);
}
```

### Active Ring Flushing (Per-Thread)

```c
void flush_all_thread_rings(ControlBlock* cb) {
    // Force submission of partially-filled rings from all threads
    uint32_t thread_count = atomic_load(&cb->thread_count);
    
    for (uint32_t i = 0; i < thread_count; i++) {
        ThreadLaneSet* tls = &cb->thread_lanes[i];
        
        if (!atomic_load(&tls->active)) continue;
        
        // Flush this thread's index lane
        uint32_t idx_active = atomic_load_explicit(&tls->index_lane.active_idx,
                                                  memory_order_acquire);
        submit_ring_for_dump(&tls->index_lane, idx_active);
        
        // Flush this thread's detail lane (if marked)
        if (atomic_load(&tls->detail_lane.marked_event_seen)) {
            uint32_t dtl_active = atomic_load_explicit(&tls->detail_lane.active_idx,
                                                      memory_order_acquire);
            submit_ring_for_dump(&tls->detail_lane, dtl_active);
        }
    }
    
    // Wait for all thread queues to drain
    wait_for_all_threads_drained(cb);
}
```

### Signal Handling

```c
static volatile sig_atomic_t g_shutdown_flag = 0;

void signal_handler(int sig) {
    g_shutdown_flag = 1;
}

void install_signal_handlers() {
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = SA_RESTART
    };
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}
```

### Duration Timer

```c
void* duration_timer_thread(void* arg) {
    TimerContext* timer = (TimerContext*)arg;
    
    struct timespec deadline = {
        .tv_sec = timer->duration_seconds,
        .tv_nsec = 0
    };
    
    // Sleep for duration
    nanosleep(&deadline, NULL);
    
    // Trigger shutdown
    atomic_store(&timer->ctx->shutdown.shutdown_requested, true);
    
    return NULL;
}
```

### Memory Ordering for Shutdown (Per-Thread)

1. **Stop Events**: RELEASE on `accepting_events = false`
2. **Thread Check**: Each thread ACQUIRE on `accepting_events` before writing
3. **Thread Flush**: Iterate all registered threads atomically
4. **Drain Signal**: RELEASE on `shutdown_requested = true`
5. **Drain Check**: ACQUIRE on `shutdown_requested` in loop
6. **Queue Empty**: All threads' queues must show `head == tail` with ACQUIRE

### Race Condition Prevention (Per-Thread)

1. **Event During Shutdown**: Each thread checks `accepting_events` before write
2. **New Thread During Shutdown**: Stop accepting new thread registrations
3. **Ring Swap During Flush**: Per-thread flush captures active index atomically
4. **Drain Thread Exit**: Join ensures all threads' pending work complete
5. **Signal During I/O**: SA_RESTART flag prevents EINTR
6. **Thread Deactivation**: Mark threads inactive before final drain

## Configuration Defaults

```c
#define DEFAULT_OUTPUT_DIR "/tmp/ada_traces"
#define DEFAULT_DURATION_SEC 0  // 0 = unlimited
#define DEFAULT_STACK_BYTES 128
#define MAX_STACK_BYTES 512
```

## Out of Scope
- Advanced triggers
- Sampling configuration
- Remote control API
- Dynamic reconfiguration

## References
- docs/specs/TRACER_SPEC.md (§CF-001 - CLI specification)
- docs/tech_designs/NATIVE_TRACER_BACKEND_ARCHITECTURE.md (Per-thread design)
- M1_E1_I2 (Per-thread ring-pool shutdown coordination)
- M1_E2_I1 (Per-thread drain lifecycle)