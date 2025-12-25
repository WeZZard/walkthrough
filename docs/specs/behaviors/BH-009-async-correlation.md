---
id: BH-009
title: Asynchronous Correlation Across Runtimes
status: active
source: docs/specs/SPAN_SEMANTICS_AND_CORRELATION.md
---

# Asynchronous Correlation Across Runtimes

## Context

**Given:**
- Modern applications use multiple asynchronous runtimes (Swift concurrency, GCD, pthreads)
- Async operations may span multiple threads
- Causality must be preserved across thread boundaries
- Each runtime has its own correlation keys and lifecycle events
- Timestamps provide total ordering but not causality
- The system must stitch together related events into logical spans

## Trigger

**When:** The Query Engine analyzes trace events to construct async causality relationships

## Outcome

**Then:**
- Correlation keys are extracted from runtime-specific events
- Logical spans are constructed by joining related events using these keys
- Cross-thread relationships are preserved
- Temporal ordering is maintained using monotonic timestamps
- Causality is represented explicitly (not inferred from thread IDs)
- Multiple runtimes can coexist in a single trace

## Correlation Keys by Runtime

### Swift Concurrency
**Keys:**
- Task ID: Unique identifier for a Swift Task
- Job pointer: Internal runtime job structure pointer
- Continuation pointer: For suspension/resume points

**Usage:**
- Task ID links all events belonging to the same async task
- Continuation pointer connects suspension ↔ resume
- Job pointer identifies individual work items in the task

**Example Flow:**
1. Task creation: Record Task ID
2. Job execution: Link Job pointer → Task ID
3. Suspension: Record Continuation pointer
4. Resume: Match Continuation pointer
5. Completion: Close Task ID span

### Grand Central Dispatch (GCD)
**Keys:**
- Block pointer: Identifies the dispatched block
- Queue label: Target queue name
- Group pointer: For dispatch_group operations

**Usage:**
- Block pointer links submit → execute
- Queue label provides execution context
- Group pointer coordinates multiple blocks

**Example Flow:**
1. dispatch_async: Record Block pointer, Queue label
2. Block execution: Match Block pointer
3. Block completion: Close span
4. dispatch_group_notify: Use Group pointer to coordinate

### POSIX Threads (pthread)
**Keys:**
- pthread_t: Thread handle
- Thread ID (TID): OS-level thread identifier
- Start routine function pointer: Entry point function

**Usage:**
- pthread_t or TID links all events on the thread
- Start routine identifies the logical operation
- Association to creator thread recorded

**Example Flow:**
1. pthread_create: Record pthread_t, start routine pointer
2. Thread start: Match start routine, begin span
3. Thread execution: All events on TID belong to span
4. Thread exit: Close span

### NSOperation (Optional)
**Keys:**
- Operation object pointer
- Queue pointer
- Dependencies (other Operation pointers)

**Usage:**
- Operation pointer identifies work unit
- Dependencies create causality graph

## Timestamp-Based Ordering

### Monotonic Clocks
All timestamps use monotonic clocks:
- macOS/iOS: mach_absolute_time (mach_continuous_time)
- Linux/Android: clock_gettime(CLOCK_BOOTTIME)
- Windows: QueryPerformanceCounter

Properties:
- Never goes backward
- Continuous through system sleep
- Cross-core synchronized
- Nanosecond precision

### Encoding
Timestamps are encoded as Protobuf Timestamp after conversion to nanoseconds.

### Cross-Thread Ordering
For logical spans that cross threads:
- Do NOT assume causality from thread IDs
- Use recorded nanosecond timestamps for ordering
- Correlation keys provide causality
- Timestamps provide temporal sequence

## Edge Cases

### Task Migration Across Threads
**Given:** A Swift async task executes on multiple threads (cooperative pool)
**When:** Correlation is performed
**Then:**
- Task ID remains constant across threads
- Thread ID changes are tracked
- Events are grouped by Task ID, not thread ID
- Timeline shows thread migrations explicitly

### Nested Async Operations
**Given:** An async operation spawns child async operations
**When:** Correlation is performed
**Then:**
- Parent-child relationships are preserved via correlation keys
- Each level has its own Task ID / Job pointer
- Parent span encompasses child spans
- Causality graph represents nesting

### Callback Hell / Chained Async
**Given:** Multiple async operations chained via callbacks
**When:** Correlation is performed
**Then:**
- Each callback has its own correlation key
- Chain is reconstructed by matching callback pointers
- Temporal ordering supplements correlation
- Gaps between callbacks are visible in timeline

### Concurrent Operations
**Given:** Multiple independent async operations run concurrently
**When:** Correlation is performed
**Then:**
- Each operation has distinct correlation keys
- No causal relationship is inferred
- Temporal overlap is visible in timeline
- Metrics show concurrency level

### Race Conditions and Ordering Ambiguity
**Given:** Two events have very close timestamps (e.g., within nanoseconds)
**When:** Ordering is determined
**Then:**
- Timestamp order is used (deterministic)
- No additional causality is inferred
- Note: True simultaneous events are rare; timestamps usually differ
- If timestamps are identical, fall back to event sequence in file

### Missing Correlation Data
**Given:** Correlation keys are not captured (e.g., due to selective persistence)
**When:** Span construction is attempted
**Then:**
- Fall back to frame spans
- Mark logical span as "unavailable"
- Use temporal proximity as a hint (not proof)
- Document limitation in query results

### Runtime Interop
**Given:** Swift async calls GCD which uses pthreads
**When:** Correlation is performed
**Then:**
- Each runtime's events are correlated using its own keys
- Cross-runtime boundaries are identified by function call relationships
- Timeline shows transitions between runtimes
- Example: Swift Task → dispatch_async → pthread_create

## Derived Span Construction

### Algorithm
1. Group events by thread (frame spans)
2. Extract correlation keys from runtime-specific events
3. Join events with matching keys (logical spans)
4. Build parent-child relationships
5. Validate temporal consistency
6. Store in derived spans table

### Validation
- Check that start timestamp < end timestamp
- Verify parent span encompasses child spans
- Flag suspicious patterns (e.g., resume before suspend)

### Storage
Derived spans are stored in a sidecar table:
```
{
  "span_id": "abc123",
  "type": "logical",
  "runtime": "swift_concurrency",
  "correlation_key": "task_id:0x1234",
  "parent_span_id": "xyz789",
  "start_ns": 1000000000,
  "end_ns": 1000005000,
  "duration_ns": 5000000,
  "thread_ids": [1, 2, 3],
  "status": "completed"
}
```

## Metrics

Track per runtime:
- Span build success rate
- Unmatched counts
- Average and p99 durations
- Number of cross-thread spans
- Number of parent-child relationships

## References

- Original: `docs/specs/SPAN_SEMANTICS_AND_CORRELATION.md` (archived source)
- Related: `BH-008-span-modeling` (Span Modeling)
- Related: `EV-002-swift-async-mapping` (Swift Concurrency Runtime Mapping)
- Related: `EV-003-gcd-mapping` (GCD Runtime Mapping)
- Related: `EV-004-pthread-mapping` (Pthread Runtime Mapping)
