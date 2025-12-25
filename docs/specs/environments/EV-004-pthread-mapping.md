---
id: EV-004
title: POSIX Threads (pthread) Runtime Mapping
status: active
source: docs/specs/runtime_mappings/PTHREAD.md
---

# POSIX Threads (pthread) Runtime Mapping

## Context

**Given:**
- POSIX threads (pthreads) are a low-level threading API
- Threads are created explicitly via `pthread_create`
- Each thread executes a start routine function
- Threads may be joinable or detached
- Correlation requires pthread_t handles, thread IDs (TID), and start routine pointers

## Trigger

**When:** The tracer instruments an application using POSIX threads

## Outcome

**Then:**
- Hooks are installed on pthread lifecycle functions
- Thread creation is tracked via `pthread_create`
- Thread start routine execution is monitored
- Thread termination is detected (return or `pthread_exit`)
- Logical spans represent thread lifetime (start → exit)
- Frame spans provide function-level detail within threads
- Correlation uses pthread_t handles and start routine pointers
- Association to creator thread is recorded

## Hook Points

### Thread Creation
- **Function:** `pthread_create`
- **Capture:** pthread_t handle (output parameter), start routine function pointer, argument pointer
- **Purpose:** Record thread creation event

### Thread Routine
- **Function:** User-provided start routine (function pointer passed to pthread_create)
- **Capture:** Entry and return of start routine
- **Purpose:** Mark logical span boundaries

### Thread Termination
- **Function:** `pthread_exit`
- **Capture:** Exit call
- **Purpose:** Mark explicit thread termination (alternative to routine return)

## Correlation Keys

### pthread_t / Thread ID (TID)
- **pthread_t:** POSIX thread handle
- **TID:** OS-level thread identifier
- Links all events on the same thread
- Used to identify which thread executed which code

### Start Routine Function Pointer
- Function pointer passed to `pthread_create`
- Identifies the logical operation/task the thread performs
- Used for span attribution

### Creator Thread
- Thread ID of the thread that called `pthread_create`
- Records parent-child relationship
- Useful for understanding thread spawning patterns

## Event and Span Mapping

### Logical Span Start
**Given:** A new thread is created
**When:** `pthread_create` is called
**Then:**
- Record creator thread ID
- Record pthread_t handle (or TID if handle not available)
- Record start routine function pointer
- Record creation timestamp
- Mark logical span start (pending execution)

### Thread Routine Entry
**Given:** The newly created thread begins executing
**When:** Start routine is entered
**Then:**
- Match start routine pointer to creation event
- Record thread entry timestamp
- Link to pthread_t / TID
- Calculate creation-to-start latency (thread spawn overhead)

### Logical Span End
**Given:** The thread completes execution
**When:** Start routine returns OR `pthread_exit` is called
**Then:**
- Record termination timestamp
- Calculate thread lifetime (exit_ts - start_ts)
- Close logical span
- Mark status as "completed" or "exited"

## Edge Cases

### pthread_exit vs. Routine Return
**Given:** A thread can terminate via return or `pthread_exit`
**When:** Termination is detected
**Then:**
- Both paths close the logical span
- `pthread_exit` may occur before routine return (early exit)
- Mark termination method in span metadata
- If `pthread_exit` is called, routine return may not occur

### Joinable vs. Detached Threads
**Given:** Threads can be created joinable or detached
**When:** Thread attributes are available
**Then:**
- Record thread type (joinable/detached) if attribute is accessible
- Joinable threads: pthread_join event may be tracked
- Detached threads: Resources freed automatically on exit
- Affects lifecycle analysis (joined threads wait for parent)

### pthread_join (Optional)
**Given:** A joinable thread completes and parent calls `pthread_join`
**When:** `pthread_join` is detected
**Then:**
- Record join timestamp
- Link to corresponding thread (via pthread_t)
- Calculate join latency (time parent waited)
- Useful for understanding thread synchronization

### Thread Cancellation
**Given:** A thread is cancelled via `pthread_cancel`
**When:** Cancellation is detected
**Then:**
- Record cancellation event
- Logical span is closed with status "cancelled"
- Routine may not return normally (cancellation point)

### Missing Start Routine Hook
**Given:** Start routine is in a stripped binary or is a generic trampoline
**When:** Hook installation is attempted
**Then:**
- Fall back to tracking via `pthread_create` and TID
- Logical span start is inferred from thread creation
- Logical span end is inferred from thread termination (if detectable)
- Attribution uses generic "pthread_start_routine" placeholder

### Recursive pthread_create
**Given:** A thread creates another thread
**When:** Correlation is performed
**Then:**
- Each thread has its own pthread_t
- Parent-child relationship is recorded (creator thread ID)
- Timeline shows thread creation hierarchy
- Useful for detecting thread pools or recursive parallelism

### Thread ID Reuse
**Given:** OS may reuse thread IDs after termination
**When:** Correlation is attempted
**Then:**
- Use timestamp ordering to disambiguate
- Match create to nearest future start (not past)
- If ambiguity remains, mark as uncertain
- pthread_t is more stable than TID for correlation

## Performance Considerations

### Lightweight Thread Tracking
- pthread lifecycle events are relatively infrequent (compared to GCD/Swift async)
- Overhead is minimal
- Capture essential data only (pthread_t, TID, start routine pointer, timestamp)

### Start Routine Hook Cost
- Start routine may be called frequently in thread pool scenarios
- Use efficient hooking (minimal prologue overhead)
- Defer detailed analysis to offline

### Thread Creation Overhead
- `pthread_create` itself is expensive (~microseconds)
- ADA overhead should be <<1% of creation cost
- Measure creation-to-start latency to detect overhead

## Data Schema

### Thread Creation Event
```c
struct ThreadCreateEvent {
    uint64_t timestamp_ns;
    uint64_t creator_tid;
    uint64_t new_pthread_t;    // or new TID
    uint64_t start_routine_ptr;
    uint32_t event_kind;       // THREAD_CREATE
};
```

### Thread Start Event
```c
struct ThreadStartEvent {
    uint64_t timestamp_ns;
    uint64_t pthread_t;        // or TID
    uint64_t start_routine_ptr;
    uint32_t event_kind;       // THREAD_START
};
```

### Thread Exit Event
```c
struct ThreadExitEvent {
    uint64_t timestamp_ns;
    uint64_t pthread_t;        // or TID
    uint32_t event_kind;       // THREAD_EXIT or THREAD_CANCEL
    uint64_t exit_code;        // if available
};
```

### Thread Join Event (Optional)
```c
struct ThreadJoinEvent {
    uint64_t timestamp_ns;
    uint64_t joiner_tid;
    uint64_t joined_pthread_t;
    uint32_t event_kind;       // THREAD_JOIN
};
```

## Query Integration

### Logical Span Construction
The Query Engine builds logical spans by:
1. Matching create and start events via pthread_t or start routine pointer
2. Matching start and exit events via pthread_t or TID
3. Recording creator thread for parent-child relationships
4. Calculating thread lifetime and spawn latency

### Metrics
- Thread spawn latency: start_ts - create_ts
- Thread lifetime: exit_ts - start_ts
- Thread count: total threads created
- Active threads: threads running at any given time
- Thread hierarchy depth: levels of thread nesting

## References

- Original: `docs/specs/runtime_mappings/PTHREAD.md` (archived source)
- Related: `BH-008-span-modeling` (Span Modeling)
- Related: `BH-009-async-correlation` (Async Correlation)
- Related: `EV-002-swift-async-mapping` (Swift Concurrency Mapping)
- Related: `EV-003-gcd-mapping` (GCD Mapping)
