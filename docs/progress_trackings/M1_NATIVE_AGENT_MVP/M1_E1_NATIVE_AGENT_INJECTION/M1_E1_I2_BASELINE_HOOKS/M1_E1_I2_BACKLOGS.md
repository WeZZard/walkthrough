# Backlogs — M1 E1 I2 Baseline Hooks

## Part A: Per-Thread Ring-Pool Implementation (PRIORITY 1)
- Implement ControlBlock with thread registry (MAX_THREADS=64)
- Create ThreadLaneSet structure with per-thread pools
- Implement thread registration on first event
- Create per-thread SPSC submit/free queues
- Implement thread-local ring swap protocol
- Add per-thread pool exhaustion handling
- Write unit tests for thread registration
- Test multi-thread scaling and isolation
- Verify zero contention between threads
- Confirm true SPSC semantics per thread

## Part B: Baseline Hooks (PRIORITY 2)
- Finalize deterministic function allowlist (portable across macOS dev boxes)
- Ensure TLS reentrancy guard correctness and low overhead
- Assign stable function IDs using hash function
- Integrate hook events with per-thread ring-pool system
- Handle ring-full conditions per thread during emission
- Use thread-local storage for lane access
- Verify minimal ABI register capture and optional 128B stack window
- Surface agent hook install summary via controller `on_message`

## Synchronization Verification (PRIORITY 1)
- Document memory ordering for per-thread queues
- Verify thread registration atomicity
- Test thread-local storage initialization
- Create stress tests with 20+ concurrent threads
- Measure per-thread ring swap latency
- Verify event ordering within each thread
- Confirm no cross-thread interference

## Metrics and Monitoring
- Track events_written, events_dropped per thread per lane
- Monitor pool_exhaustion_count per thread
- Log ring swap frequency by thread ID
- Measure overhead scaling with thread count
- Report thread registration failures
- Track active thread count