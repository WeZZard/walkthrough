---
id: EV-003
title: Grand Central Dispatch (GCD) Runtime Mapping
status: active
source: docs/specs/runtime_mappings/GCD.md
---

# Grand Central Dispatch (GCD) Runtime Mapping

## Context

**Given:**
- GCD (Grand Central Dispatch) is Apple's queue-based concurrency framework
- Blocks are submitted to queues and executed asynchronously
- Logical spans (submit → execute → complete) differ from frame spans
- Correlation requires Block pointers, Queue labels, and Group pointers
- GCD uses both public APIs and private runtime symbols

## Trigger

**When:** The tracer instruments an application using GCD (dispatch queues)

## Outcome

**Then:**
- Hooks are installed on GCD submit and execute functions
- Block pointers are captured for correlation
- Queue labels provide execution context
- Group pointers coordinate multiple blocks (for dispatch_group operations)
- Logical spans are constructed from submit → execute → complete lifecycle
- Frame spans provide machine-level detail
- Events use monotonic timestamps
- Performance overhead is minimal

## Symbols and Functions to Instrument

### Public API (Submit)
- `dispatch_async` - Asynchronous block submission
- `dispatch_sync` - Synchronous block submission (blocks caller)
- `dispatch_barrier_async` - Barrier block submission
- `dispatch_group_enter` - Group entry
- `dispatch_group_leave` - Group exit
- `dispatch_group_notify` - Group completion callback

### Private/Internal API (Execute)
- `_dispatch_client_callout` - Block execution trampoline (or similar)
- Block invoke trampoline - Actual block execution

**Note:** Private symbols may vary by macOS/iOS version. Use dynamic resolution with fallback.

## Correlation Keys

### Block Pointer
- Unique identifier for a dispatched block
- Captured at submit (dispatch_async, etc.)
- Matched at execute (_dispatch_client_callout or block invoke)
- Links submission and execution events

### Queue Label
- String identifier for the dispatch queue
- Available from queue structure
- Provides execution context (e.g., "com.example.imageProcessing")
- Helps identify concurrency patterns

### Group Pointer (for dispatch_group)
- Identifies a group of related blocks
- Captured at group operations (enter, leave, notify)
- Coordinates completion of multiple blocks
- Links blocks that must complete before notify callback

## Event and Span Mapping

### Logical Span Start (Submit)
**Given:** A block is submitted to a dispatch queue
**When:** `dispatch_async` (or similar) is called
**Then:**
- Record Block pointer
- Record Queue label
- Record timestamp (submit time)
- Mark logical span start
- Note: Submit returns immediately (does not block)

### Logical Span Execute
**Given:** A dispatched block begins execution
**When:** `_dispatch_client_callout` or block invoke is called
**Then:**
- Match Block pointer to submission event
- Record execution start timestamp
- Record executing thread ID
- Calculate queue latency (execute_start_ts - submit_ts)

### Logical Span End
**Given:** A block completes execution
**When:** Callout or block invoke returns
**Then:**
- Record completion timestamp
- Calculate execution duration (complete_ts - execute_start_ts)
- Close logical span
- Total latency = complete_ts - submit_ts

### Dispatch Group Operations
**Given:** Multiple blocks are coordinated via dispatch_group
**When:** Group operations are detected
**Then:**
- `dispatch_group_enter`: Increment group counter
- `dispatch_group_leave`: Decrement group counter
- `dispatch_group_notify`: Record notify callback, triggered when counter reaches zero
- Link all blocks in the group via Group pointer
- Notify callback is itself a block (follows same lifecycle)

## Edge Cases

### dispatch_sync (Synchronous)
**Given:** `dispatch_sync` is called instead of dispatch_async
**When:** Events are analyzed
**Then:**
- Logical span is similar to dispatch_async
- BUT: Caller is blocked until completion
- Execution may happen on caller thread (queue-dependent)
- Submit and execute may be very close in time
- Mark as "sync" dispatch for differentiation

### Nested Dispatch
**Given:** A block dispatches another block
**When:** Correlation is performed
**Then:**
- Each block has its own Block pointer
- Parent-child relationship may be inferred from call stack
- Timeline shows nested dispatch pattern
- Useful for detecting dispatch cascades

### Barrier Blocks
**Given:** `dispatch_barrier_async` is used
**When:** Execution is analyzed
**Then:**
- Barrier block executes alone (no concurrent blocks on same queue)
- Mark as "barrier" dispatch
- Timeline shows exclusive execution window
- Helps identify synchronization points

### Queue Targeting (Target Queues)
**Given:** A queue targets another queue (queue hierarchy)
**When:** Execution occurs
**Then:**
- Block may execute on target queue, not submission queue
- Queue label reflects actual execution queue
- Trace shows queue hierarchy if available

### Missing Private Symbols
**Given:** Private GCD symbols are not found (stripped binary, version change)
**When:** Hook installation is attempted
**Then:**
- Log warning about missing symbols
- Fall back to public API hooks only
- Can still track submit, but not execute timing
- Document limitation: queue latency unavailable

### Block Pointer Reuse
**Given:** Block pointers may be reused after completion
**When:** Correlation is attempted
**Then:**
- Use timestamp ordering to disambiguate
- Match submit to nearest future execute (not past)
- If ambiguity remains, mark as uncertain

### Group Notify Without Complete
**Given:** `dispatch_group_notify` is registered but group never reaches zero
**When:** Trace analysis occurs
**Then:**
- Notify callback never executes
- Mark as "pending" or "incomplete"
- May indicate a bug (forgot dispatch_group_leave)

## Performance Considerations

### Hot Path Optimization
- GCD is heavily used; minimize overhead
- Use shared-memory ring buffer
- Capture only essential data (Block pointer, Queue label, timestamp)
- Defer detailed analysis to offline

### Queue Label Storage
- Queue labels are strings (expensive to capture repeatedly)
- Capture once per queue, assign queue ID
- Use queue ID in events (4-8 bytes vs. string)
- Maintain queue ID → label mapping in metadata

### Private Symbol Resilience
- Private symbols may change between OS versions
- Use symbol resolution at runtime
- Gracefully degrade if symbols unavailable
- Document which OS versions support full tracing

## Data Schema

### Dispatch Submit Event
```c
struct DispatchSubmitEvent {
    uint64_t timestamp_ns;
    uint64_t block_ptr;
    uint32_t queue_id;       // or queue label hash
    uint32_t event_kind;     // DISPATCH_ASYNC, DISPATCH_SYNC, etc.
    uint64_t thread_id;      // submitting thread
};
```

### Dispatch Execute Event
```c
struct DispatchExecuteEvent {
    uint64_t timestamp_ns;
    uint64_t block_ptr;
    uint32_t queue_id;
    uint32_t event_kind;     // DISPATCH_EXECUTE_START or COMPLETE
    uint64_t thread_id;      // executing thread
};
```

### Dispatch Group Event
```c
struct DispatchGroupEvent {
    uint64_t timestamp_ns;
    uint64_t group_ptr;
    uint32_t event_kind;     // GROUP_ENTER, GROUP_LEAVE, GROUP_NOTIFY
    uint64_t thread_id;
};
```

## Query Integration

### Logical Span Construction
The Query Engine builds logical spans by:
1. Matching submit and execute events via Block pointer
2. Computing queue latency (time waiting in queue)
3. Computing execution duration
4. Grouping blocks by Queue label
5. Linking blocks by Group pointer (for dispatch_group operations)

### Metrics
- Queue latency: execute_start_ts - submit_ts
- Execution duration: complete_ts - execute_start_ts
- Total latency: complete_ts - submit_ts
- Queue depth: concurrent blocks on same queue
- Group completion time: last block complete in group

## References

- Original: `docs/specs/runtime_mappings/GCD.md` (archived source)
- Related: `BH-008-span-modeling` (Span Modeling)
- Related: `BH-009-async-correlation` (Async Correlation)
- Related: `EV-002-swift-async-mapping` (Swift Concurrency Mapping)
- Related: `EV-004-pthread-mapping` (Pthread Mapping)
