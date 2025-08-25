# Test Plan — M1 E1 I2 Baseline Hooks

## Goals
Validate per-thread ring-pool architecture works correctly with true SPSC semantics and deterministic hooks fire to produce index events through the per-thread pool system.

## Part A: Per-Thread Ring-Pool Tests

### Functional Tests
1. **Thread Registration**
   - First event triggers thread registration
   - Verify unique thread slot assignment
   - Check thread-local lane initialization
   - Confirm MAX_THREADS limit enforced

2. **Per-Thread Ring Fill and Swap**
   - Each thread fills its own rings independently
   - Verify automatic swap within thread's pool
   - Confirm no interference between threads
   - Check thread-specific metrics update

3. **Per-Thread Pool Exhaustion**
   - Fill all rings in one thread's pool
   - Verify drop-oldest for that thread only
   - Other threads continue unaffected
   - Check per-thread exhaustion counters

4. **Multi-Thread Scaling**
   - Spawn 10+ threads writing events
   - Each thread uses own ring pool
   - No contention or cache line bouncing
   - Verify true SPSC semantics per thread

### Synchronization Tests
1. **Memory Ordering Verification**
   - Run with ThreadSanitizer enabled
   - No data races detected
   - Proper acquire/release semantics

2. **Queue Operations**
   - Submit queue wraparound
   - Free queue wraparound
   - Full/empty detection correct
   - No ABA problems

## Part B: Baseline Hook Tests

### Cases
- `test_cli`: expect calls to `fibonacci`, `process_file`, `calculate_pi`, `recursive_function`
- `test_runloop`: expect `timer_callback`, `dispatch_work`, `signal_handler` events
- Both: non-zero events, proper call depth tracking, no crashes

### Procedure
1. Initialize control block with thread registry
2. Spawn multi-threaded test process
3. Run for 3–5s duration
4. Verify per-thread behavior:
   - Each thread gets unique lane set
   - Events flow through thread's own pool
   - Ring swaps isolated per thread
   - No inter-thread contention
5. Check persisted file:
   - Events from all threads present
   - Thread IDs correctly recorded
   - Timestamps monotonic per thread
   - Expected function IDs from all threads

## Performance Tests
1. **Per-Thread Throughput**
   - Each thread generates 100K events/sec
   - No degradation with thread count
   - Measure overhead < 1% per thread
   - No cross-thread interference

2. **Contention Analysis**
   - Zero cache line bouncing between threads
   - True lock-free within each thread
   - Compare 1 vs 10 vs 20 threads
   - Linear scaling expected

3. **Latency**
   - Ring swap time < 1ms per thread
   - Event write time < 50ns (better than shared)

## Acceptance Criteria
- All per-thread ring-pool tests pass
- ThreadSanitizer clean (no races)
- Events captured from all threads
- No crashes during 5-minute multi-thread stress
- Per-thread stats show:
  - events_written > 0 for each active thread
  - events_dropped = 0 (nominal load)
  - pool_exhaustion isolated to heavy threads
- Thread count scales to MAX_THREADS (64)
- Zero contention measured between threads
- Hooks report success via on_message callback