---
id: BH-007
title: Selective Persistence (Flight Recorder Mode)
status: active
source: docs/specs/TRACER_SPEC.md
---

# Selective Persistence (Flight Recorder Mode)

## Context

**Given:**
- Capturing full detail for all events would generate excessive data volume
- Most events are routine and do not require deep context
- Certain events are significant and warrant detailed capture (registers, stack)
- The system should operate like a flight recorder: always capturing, selectively persisting
- Users need to configure when detail should be persisted

## Trigger

**When:** The tracer operates in selective persistence mode with configured triggers

## Outcome

**Then:**
- The index lane always captures and persists all events (lightweight, 32 bytes per event)
- The detail lane always captures rich context to a ring buffer (512 bytes per event)
- Detail events are persisted only when marked events occur (windowed persistence)
- Marking policy determines which events are "marked"
- Pre-roll and post-roll windows define how much context is saved around marked events
- Triggers can be configured via CLI/API without restart
- The trace manifest records window boundaries and trigger metadata

## Two-Lane Architecture

### Index Lane (Always-On Persistence)
- Captures ALL function calls and returns
- Fixed 32-byte events (timestamp, function_id, thread_id, event_kind, call_depth, detail_seq)
- Always written to disk (no selective logic)
- Provides complete execution timeline
- Enables correlation with detail events via `detail_seq` field

### Detail Lane (Always-Captured, Selectively Persisted)
- Captures rich context for ALL events to ring buffer
- 512-byte events (header + ARM64 registers + stack snapshot)
- Ring buffer is always active (continuous capture)
- Persistence triggered only when marked events occur
- Uses bounded ring-pool for windowed persistence

## Marking Policy

A marking policy defines which events are considered "marked". Examples:

### Symbol-Based
- Mark calls to specific functions: `--trigger symbol=malloc,free`
- Mark returns from functions with error codes
- Mark exceptions or signal handlers

### Performance-Based
- Mark functions exceeding latency threshold: `--trigger p99=function:processImage>50ms`
- Mark functions with high CPU usage
- Mark functions with unexpected patterns

### Time-Based
- Mark events during specific time windows: `--trigger time=10:30-10:45`
- Mark events during known problematic periods

### System Events
- Mark events when crashes occur: `--trigger crash`
- Mark events when signals are delivered (SIGSEGV, SIGABRT, etc.)
- Mark events when process terminates abnormally

## Windowing

When a marked event occurs, the system persists:

### Pre-Roll Window
- Events captured BEFORE the trigger
- Typically 1000 events or configurable seconds (e.g., `--pre-roll-sec=5`)
- Provides context leading up to the marked event
- Captured from the ring buffer (already in memory)

### Post-Roll Window
- Events captured AFTER the trigger
- Typically 1000 events or configurable seconds (e.g., `--post-roll-sec=5`)
- Provides outcome/consequence of the marked event
- Captured as they occur after trigger fires

### Window Recording
Each persistence window is recorded in the manifest:
```json
{
  "windows": [
    {
      "startNs": 1234567890000,
      "endNs": 1234567900000,
      "triggerKind": "symbol:malloc",
      "keySymbolVersion": "v1.2.3-abc123"
    }
  ]
}
```

## Edge Cases

### Multiple Triggers in Quick Succession
**Given:** Marked events occur rapidly (e.g., every 100ms)
**When:** Multiple triggers fire before the post-roll completes
**Then:**
- Windows may overlap
- The system coalesces overlapping windows into a single persistence range
- The manifest records the merged window with all trigger kinds
- This prevents excessive duplicate data

### Trigger During Startup
**Given:** A marked event occurs during agent initialization
**When:** The pre-roll window is requested
**Then:**
- The system persists as much pre-roll as available in the ring buffer
- If the ring buffer hasn't wrapped yet, partial pre-roll is saved
- The manifest records actual pre-roll duration (may be less than configured)

### Trigger Near Process Exit
**Given:** A marked event occurs just before process termination
**When:** The post-roll window is requested
**Then:**
- The system persists events until the process exits
- Post-roll may be shorter than configured
- The manifest records actual post-roll duration

### No Marked Events
**Given:** A tracing session runs with selective persistence enabled
**When:** No marked events occur during the session
**Then:**
- The index lane is fully persisted (complete timeline)
- The detail lane file is NOT created (no detail.atf)
- The manifest indicates no detail persistence windows
- This is normal and expected for sessions with no interesting events

### Ring Buffer Overflow Before Dump
**Given:** Detail ring buffer overflows before a marked event occurs
**When:** Events are being captured
**Then:**
- Oldest events are overwritten (drop-oldest)
- When a marked event occurs, pre-roll may have less than configured duration
- The manifest records actual available pre-roll
- This indicates event rate exceeds ring size; user should increase ring size or reduce event rate

## Configuration

Selective persistence is configured via:

### CLI Options
```bash
ada trace <binary> \
  --pre-roll-sec=5 \
  --post-roll-sec=5 \
  --trigger symbol=malloc \
  --trigger p99=function:processImage>50ms \
  --trigger crash \
  --trigger time=10:30-10:45
```

### Key-Symbol Updates (Live)
- Marking policy can be updated without restart
- Uses RCU (Read-Copy-Update) pointer swap
- New policy applies to future events
- Manifest records policy version and hash

## Manifest Recording

The trace manifest includes:
```json
{
  "mode": "selective_persistence",
  "index_lane": {
    "always_persisted": true,
    "event_count": 1000000
  },
  "detail_lane": {
    "capture": "always",
    "persistence": "windowed",
    "windows": [
      {
        "startNs": 1234567890000,
        "endNs": 1234567900000,
        "triggerKind": "symbol:malloc",
        "preRollSec": 5.0,
        "postRollSec": 5.0
      }
    ],
    "event_count": 50000,
    "coverage_ratio": 0.05
  },
  "marking_policy": {
    "version": "v1.2.3",
    "hash": "abc123",
    "rules": [
      { "type": "symbol", "pattern": "malloc" },
      { "type": "crash", "enabled": true }
    ]
  }
}
```

## Query Integration

The Query Engine (BH-012) uses window metadata to:
- Prefer detail-lane data within persisted windows
- Fall back to index-lane summaries outside windows
- Surface window boundaries in narratives
- Report detail coverage ratio (e.g., "5% of events have detail")

## References

- Original: `docs/specs/TRACER_SPEC.md` (archived source - sections TD-005, CF-004, A6)
- Related: `BH-005-ring-buffer` (Ring Buffer with lane-specific semantics)
- Related: `BH-006-backpressure` (Backpressure Handling)
- Related: `BH-012-narrative-generation` (Narrative Generation with selective persistence support)
