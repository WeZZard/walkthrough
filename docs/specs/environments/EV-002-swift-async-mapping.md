---
id: EV-002
title: Swift Concurrency Runtime Mapping
status: active
source: docs/specs/runtime_mappings/SWIFT_CONCURRENCY.md
---

# Swift Concurrency Runtime Mapping

## Context

**Given:**
- Swift async/await uses a cooperative task runtime
- Tasks may suspend and resume across different threads
- Logical spans (task execution) differ from frame spans (function calls)
- Correlation requires Task IDs, Job pointers, and Continuation pointers
- The runtime uses internal symbols that may vary by Swift version

## Trigger

**When:** The tracer instruments a Swift application using async/await

## Outcome

**Then:**
- Hooks are installed on Swift runtime symbols
- Task IDs and Job pointers are captured for correlation
- Continuation pointers link suspension and resume points
- Logical spans are constructed from task lifecycle events
- Frame spans provide machine-level detail (prologue/epilogue)
- Async function attribution uses backtrace at job execution
- Events use monotonic timestamps (mach_absolute_time on macOS)
- Performance overhead is minimal (shared-memory ring buffer, minimal per-event work)

## Symbols to Instrument

### Core Symbols
Symbol names may vary by Swift version:
- `swift_task_create` - Task creation
- `swift_task_enqueue` - Task submission to executor
- `swift_job_run` - Job execution start
- `swift_continuation_init` - Suspension point
- `swift_continuation_resume` - Resume from suspension
- `swift_task_future_complete` - Task completion

### Optional Symbols (Enhanced Tracing)
- `swift_task_group_*` - Task group operations
- `swift_task_enqueueOnExecutor` - Explicit executor targeting

**Note:** Symbol names are internal and may change. Use symbol resolution with fallback for missing symbols.

## Correlation Keys

### Task ID / Job Pointer
- Unique identifier for a Swift Task
- Extracted from `swift_job_run` argument
- Persists across thread migrations
- Used to link all events belonging to the same task

### Continuation Pointer
- Identifies a specific suspension point
- Captured at `swift_continuation_init`
- Matched at `swift_continuation_resume`
- Links suspension and resume events

## Event and Span Mapping

### Logical Span Start
**Given:** A Swift async function begins execution
**When:** `swift_job_run` is called
**Then:**
- Record Job pointer / Task ID
- Capture short backtrace to attribute top Swift frame
- This identifies the async function name
- Mark logical span start with Task ID

### Suspension
**Given:** An async function suspends (e.g., awaiting another task)
**When:** `swift_continuation_init` is called
**Then:**
- Capture Continuation pointer
- Mark current logical span as suspended
- Record suspension timestamp
- Preserve partial span data

### Resume
**Given:** A suspended task is resumed
**When:** `swift_continuation_resume` is called with Continuation pointer
**Then:**
- Match Continuation pointer to suspension event
- Advance task state from suspended to running
- Record resume timestamp
- Calculate suspension duration (resume_ts - suspend_ts)

### Logical Span End
**Given:** A Swift async function completes
**When:** Final resume completion or `swift_task_future_complete`
**Then:**
- Close logical span
- Record total duration
- Mark status as "completed"
- Store correlation data for query access

## Edge Cases

### Thread Migration
**Given:** A Swift Task executes on multiple threads (cooperative pool)
**When:** Events are correlated
**Then:**
- Task ID remains constant across threads
- Thread ID changes are tracked in events
- Events are grouped by Task ID, not thread ID
- Timeline shows which threads executed the task

### Nested Tasks
**Given:** An async function spawns child tasks
**When:** Correlation is performed
**Then:**
- Parent-child relationships are preserved
- Each child task has its own Task ID
- Parent Task ID can be captured at task creation if available
- Causality graph shows nesting

### Multiple Suspensions
**Given:** A task suspends multiple times
**When:** Tracking suspensions
**Then:**
- Each suspension gets a unique Continuation pointer
- Suspensions are tracked sequentially
- Total suspended time = sum of all suspension durations
- Timeline shows all suspension/resume cycles

### Task Cancellation
**Given:** A Swift Task is cancelled
**When:** Cancellation is detected (via Task.isCancelled or cancellation event)
**Then:**
- Logical span is closed immediately
- Status marked as "canceled"
- Partial execution time is recorded
- Outstanding continuations are noted as cancelled

### Missing Symbols
**Given:** Swift runtime symbols are not found (stripped binary, version mismatch)
**When:** Hook installation is attempted
**Then:**
- Log warning about missing symbols
- Fall back to frame spans only
- Logical span construction is unavailable
- Document limitation in trace metadata

### Backtracing for Function Name
**Given:** Async function name is not directly available in `swift_job_run`
**When:** Logical span attribution is needed
**Then:**
- Capture short backtrace (e.g., top 5 frames)
- Identify top Swift frame (skip runtime frames)
- Use frame's function_id for attribution
- This provides async function name

### Continuation Mismatch
**Given:** A resume event with unknown Continuation pointer
**When:** Correlation is attempted
**Then:**
- Log warning about orphaned resume
- Create unmatched event
- Do not crash or fail the trace
- May indicate suspension event was dropped or out of window

## Performance Considerations

### Hot Path Optimization
- Swift runtime symbols are called very frequently
- Use shared-memory ring buffer (no syscalls in hot path)
- Minimize per-event work (just capture pointers and timestamp)
- Defer correlation to offline analysis

### Backtrace Cost
- Backtracing is expensive (~microseconds)
- Only capture at `swift_job_run` (once per task)
- Keep backtrace shallow (5-10 frames max)
- Skip runtime frames to reduce size

### Timestamp Precision
- Use monotonic clock (mach_absolute_time on macOS)
- ~20-40ns overhead on Apple Silicon
- Sufficient precision for async timing
- Continuous through system sleep

## Data Schema

### Task Execution Event (at swift_job_run)
```c
struct TaskExecutionEvent {
    uint64_t timestamp_ns;
    uint64_t task_id;         // or job pointer
    uint64_t thread_id;
    uint32_t event_kind;      // TASK_START
    uint64_t function_id;     // from backtrace
    uint64_t backtrace[5];    // optional
};
```

### Continuation Event (at suspend/resume)
```c
struct ContinuationEvent {
    uint64_t timestamp_ns;
    uint64_t continuation_ptr;
    uint64_t task_id;
    uint32_t event_kind;      // SUSPEND or RESUME
    uint64_t thread_id;
};
```

## Query Integration

### Logical Span Construction
The Query Engine builds logical spans by:
1. Grouping events by Task ID
2. Matching suspension/resume pairs via Continuation pointer
3. Computing total duration and suspended time
4. Building parent-child relationships if available

### Preferred Span Type
For Swift async functions:
- Prefer logical span (task-level)
- Frame spans available for low-level analysis
- Cross-reference both for comprehensive view

## References

- Original: `docs/specs/runtime_mappings/SWIFT_CONCURRENCY.md` (archived source)
- Related: `BH-008-span-modeling` (Span Modeling)
- Related: `BH-009-async-correlation` (Async Correlation)
- Related: `EV-003-gcd-mapping` (GCD Mapping)
- Related: `EV-004-pthread-mapping` (Pthread Mapping)
