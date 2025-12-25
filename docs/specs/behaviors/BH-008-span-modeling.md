---
id: BH-008
title: Span Modeling and Pairing Rules
status: active
source: docs/specs/SPAN_SEMANTICS_AND_CORRELATION.md
---

# Span Modeling and Pairing Rules

## Context

**Given:**
- Function execution can be modeled as spans (time intervals with start and end)
- There are two types of spans: frame spans (machine-level) and logical spans (runtime-level)
- Different execution models (sync, async, threads) require different pairing rules
- Some spans may be unmatched due to cancellation, crashes, or other exceptional flows
- Spans need timeout handling for long-running or incomplete operations

## Trigger

**When:** The system analyzes trace events to construct execution spans

## Outcome

**Then:**
- Frame spans are delimited by machine-level function entry/return (prologue/epilogue)
- Logical spans are delimited by runtime-level start/complete of async jobs, tasks, blocks, or operations
- Logical spans take precedence over frame spans for the same high-level function when both are available
- When logical spans are absent, the system falls back to frame spans
- Span pairing follows runtime-specific rules
- Unmatched spans are allowed with annotated reasons when detectable
- Timeouts are applied to detect incomplete spans

## Span Types

### Frame Span
- Delimited by machine-level function entry/return
- Always emitted when hooks exist
- Captures exact machine-level execution
- Suitable for low-level analysis

### Logical Span
- Delimited by runtime-level start/complete of async operations
- Preferred for narratives and high-level analysis
- Better represents programmer intent
- Hides runtime scheduling details

### Precedence
When both span types exist for the same high-level function:
1. Use logical span for narratives
2. Use frame span for machine-level analysis
3. Cross-reference between both when needed

## Pairing Rules by Runtime

### Synchronous Functions
**Given:** A synchronous function call
**When:** Events are analyzed
**Then:**
- Pair: frame ENTRY ↔ frame RETURN
- Match by function_id, thread_id, and call_depth
- Typically well-matched (exceptions noted below)

### Swift Concurrency (Async/Await)
**Given:** A Swift async function
**When:** Events are analyzed
**Then:**
- Pair: job start ↔ final continuation resume completion
- Use correlation keys: Task ID, Job pointer, Continuation pointer
- May span multiple threads (follow Task ID)
- Suspensions are intermediate states (not end of span)

### Grand Central Dispatch (GCD)
**Given:** A dispatch_async call
**When:** Events are analyzed
**Then:**
- Pair: submit ↔ block execute complete
- Use correlation keys: Block pointer, Queue label, Group pointer
- Submit returns immediately (different from execute)
- For groups: dispatch_group_leave/notify drive completion

### POSIX Threads (pthread)
**Given:** A pthread_create call
**When:** Events are analyzed
**Then:**
- Pair: thread start routine entry ↔ return (or pthread_exit)
- Use correlation keys: pthread_t, thread ID, start routine function pointer
- Record association to creator thread via pthread_create site
- Joinable vs detached affects lifecycle

### Callback Patterns
**Given:** An initiating call with a callback
**When:** Events are analyzed
**Then:**
- Pair: initiating call ↔ callback entry/return
- Correlation depends on runtime (function pointer, closure, block)
- May be cross-thread

## Edge Cases

### Unmatched Spans
**Given:** A span has an entry but no matching return
**When:** The span is analyzed
**Then:**
- Mark status as "unmatched"
- Annotate reason when detectable:
  - Cancellation (e.g., Swift Task.cancel)
  - Process crash or termination
  - Tail-call optimization
  - longjmp or exception unwinding
  - Timeout (see below)
- Preserve partial span with available data
- Do not discard unmatched spans (they provide valuable debugging info)

### Cancellation
**Given:** A span is explicitly cancelled (e.g., Swift Task cancellation)
**When:** Cancellation event is detected
**Then:**
- Close the span immediately
- Set reason = "canceled"
- Record cancellation event details if available

### Timeouts
**Given:** A span remains open without a matching end event
**When:** Timeout period elapses (default 5 seconds, configurable)
**Then:**
- Mark span as "open" in derived index
- If the span completes later, update status and annotate resumed time
- This handles long-running operations without prematurely closing spans

### Tail Calls
**Given:** A function ends with a tail call
**When:** Pairing is attempted
**Then:**
- The original function may have ENTRY but no RETURN
- Mark as unmatched with reason "tail_call" if detectable
- The tail-called function will have its own span

### Exceptions and longjmp
**Given:** Stack unwinding occurs due to exception or longjmp
**When:** Multiple functions are skipped
**Then:**
- Multiple spans may be unmatched (missing RETURN events)
- Annotate reason = "exception" or "unwind"
- Exception event provides context for all affected spans

### Process Exit
**Given:** The process exits before all functions return
**When:** End-of-trace is reached
**Then:**
- All remaining open spans are marked "unmatched"
- Annotate reason = "process_exit"
- This is normal at trace end

### Cross-Thread Spans
**Given:** A logical span starts on one thread and completes on another
**When:** Span construction is attempted
**Then:**
- Do not use thread ID for matching
- Use correlation keys (Task ID, Job pointer, etc.)
- Maintain ordering with recorded nanosecond timestamps
- Do not assume causality from thread IDs alone

## Timeout Configuration

Default unmatched timeout: 5 seconds (configurable)
- Spans open longer than timeout are considered "open"
- Annotation added upon closure if resumed later
- Prevents premature span closure for long operations
- Adjustable based on workload characteristics

## Event Mapping to ATF V4

### Frame Spans
- Represented as FunctionCall/FunctionReturn events
- Directly captured by TRACER_SPEC prologue/epilogue hooks
- Stored in index lane (always) and detail lane (selective)

### Logical Spans (MVP)
- Emitted as frame events on synthesized function names
- Correlation metadata stored in extensions or derived indexes
- MVP: emit frame events, derive logical spans offline
- Post-MVP: may emit dedicated logical span events

## Derived Span Construction

The Query Engine builds spans by:

### Per-Thread Shadow Stacks
- Build shadow stack for each thread from frame events
- Track call depth and match ENTRY ↔ RETURN
- Detect unmatched spans via depth inconsistencies

### Logical Span Joining
- Join runtime-specific events using correlation keys
- See runtime mapping docs (EV-002, EV-003, EV-004) for details
- Create derived span records (indexed, not re-emitted)
- Store in derived spans table for query access

## References

- Original: `docs/specs/SPAN_SEMANTICS_AND_CORRELATION.md` (archived source)
- Related: `EV-002-swift-async-mapping` (Swift Concurrency Mapping)
- Related: `EV-003-gcd-mapping` (GCD Mapping)
- Related: `EV-004-pthread-mapping` (Pthread Mapping)
- Related: `BH-009-async-correlation` (Async Correlation)
