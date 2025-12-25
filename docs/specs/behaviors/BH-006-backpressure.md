---
id: BH-006
title: Backpressure and Overflow Handling
status: active
source: docs/specs/TRACER_SPEC.md
---

# Backpressure and Overflow Handling

## Context

**Given:**
- The tracer may experience event rates exceeding sustainable throughput
- System resources (memory, disk I/O, CPU) are finite
- The tracer must maintain bounded latency under overload
- Events have different priorities (e.g., crash diagnostics vs. routine calls)
- The system should not introduce unbounded queues or sampling

## Trigger

**When:** The event production rate exceeds the sustainable drain and persistence rate

## Outcome

**Then:**
- Per-thread SPSC rings feed into an MPSC drain queue (L1 → L2)
- High and low watermarks are enforced per ring buffer
- A bounded pool of fixed-size rings per lane achieves bounded spill
- No random sampling or quality shedding is applied
- If overflow persists beyond pool capacity, drop-oldest policy is used on the lowest-priority lane only
- Explicit reason codes are recorded for all drops (e.g., ring full, encoder lag)
- Latency remains bounded even under severe overload
- A must-not-drop lane exists for crash and signal diagnostics

## Watermark System

### High Watermark
- Threshold at which backpressure warnings are generated
- Typically set at 80-90% of ring capacity
- Triggers aggressive drain attempts
- Does not cause drops (only warnings)

### Low Watermark
- Threshold at which backpressure warnings clear
- Typically set at 40-60% of ring capacity
- Indicates system has recovered from overload
- Hysteresis prevents flapping between states

## Bounded Spill Mechanism

Instead of growing queues unboundedly:
- Each lane has a fixed pool of K spare rings
- When active ring fills, it's swapped for a spare
- The pool size bounds maximum memory usage
- If pool is exhausted, drop-oldest is applied
- No sampling or event quality degradation occurs

## Drop Policy

When the ring pool is exhausted and drops are necessary:

### Priority Ordering (Lowest to Highest)
1. Index lane routine events (may be dropped)
2. Detail lane events (preferably preserved)
3. Must-not-drop lane (crash/signal diagnostics - never dropped)

### Drop Mechanism
- Drop-oldest: Overwrite the oldest events in the ring buffer
- Circular buffer semantics ensure newest events are preserved
- Dropped event count is tracked per reason code

### Reason Codes
Explicit tracking of why events were dropped:
- `RING_FULL`: Ring buffer capacity exceeded
- `ENCODER_LAG`: Encoder cannot keep up with event rate
- `DISK_SLOW`: Disk I/O is slower than expected
- `POOL_EXHAUSTED`: All spare rings in use
- `MEMORY_LIMIT`: System memory constraints

## Edge Cases

### Sustained Overload
**Given:** Event rate remains above sustainable throughput for an extended period
**When:** All spare rings are consumed
**Then:**
- The active ring continues to accept events
- Drop-oldest policy is applied to the active ring
- `pool_exhausted_count` metric is incremented
- The system maintains bounded latency (does not block producers)
- Events are dropped with reason code `POOL_EXHAUSTED`
- When drain catches up, spare rings are returned to the pool
- Normal operation resumes automatically

### Encoder Lag
**Given:** The Protobuf encoder or disk I/O is slower than expected
**When:** Rings are submitted faster than they can be persisted
**Then:**
- The submit queue grows up to its bounded capacity
- When the queue is full, the ring pool appears exhausted to the agent
- Events are dropped with reason code `ENCODER_LAG`
- The system does not block event production
- Metrics clearly indicate encoder as the bottleneck

### Crash Diagnostics Priority
**Given:** A crash or signal is detected in the target process
**When:** The must-not-drop lane needs to capture diagnostic events
**Then:**
- Events in the must-not-drop lane are never discarded
- If necessary, space is reclaimed from lower-priority lanes
- Crash diagnostics are guaranteed to be captured
- This ensures root cause analysis is always possible

### Bursty Workloads
**Given:** Event rate has sudden spikes (e.g., 100M events in 1 second, then quiet)
**When:** The spike occurs
**Then:**
- Spare rings absorb the burst
- Pre-configured burst window (Tb) determines pool size
- If burst exceeds Tb × Rp capacity, drop-oldest applies
- System recovers automatically once burst subsides
- Metrics show peak event rate and pool utilization

### Watermark Flapping Prevention
**Given:** Event rate oscillates around the high watermark
**When:** The rate fluctuates
**Then:**
- Hysteresis between high and low watermarks prevents flapping
- Warnings are issued only on high watermark crossing (upward)
- Warnings are cleared only on low watermark crossing (downward)
- This avoids excessive metric/log noise

## No Sampling Policy

The system explicitly does NOT use:
- Random sampling of events
- Probabilistic event selection
- Event quality degradation (e.g., reduced detail)
- Adaptive sampling based on load

Rationale:
- Sampling introduces non-determinism harmful to debugging
- Drop-oldest with bounded memory is more predictable
- Users can configure exclusions if overhead is too high
- Full coverage is the default; users opt into exclusions explicitly

## Configuration

The backpressure system can be configured with:
- Target peak rate (Rp) in events/second
- Burst window (Tb) in seconds
- Ring pool size (K) per lane
- High watermark percentage (e.g., 85%)
- Low watermark percentage (e.g., 50%)

Default calculation:
- K_index = 2-3 spare rings for index lane
- K_detail = 1-2 spare rings for detail lane
- Ring size = (Rp × Tb) / K

## Metrics

Tracked per lane:
- Events written (total count)
- Events dropped (total count)
- Drops by reason code (RING_FULL, ENCODER_LAG, etc.)
- Pool free minimum (watermark)
- Pool exhausted count
- High watermark crossings
- Low watermark crossings
- Current fill percentage

System-wide:
- Aggregate events/second
- Aggregate drops/second
- Encoder latency percentiles (p50, p95, p99)
- Disk write throughput

## References

- Original: `docs/specs/TRACER_SPEC.md` (archived source - sections BP-001 to BP-004)
- Related: `BH-005-ring-buffer` (Ring Buffer Management)
- Related: `BH-007-selective-persistence` (Selective Persistence)
