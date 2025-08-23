# M1 E2: Index Pipeline

## Goal
Drain index-lane events from shared memory ring buffers and persist to a durable binary file with a small header.

## Deliverables
- Header format (magic, record size, reserved)
- Batched drain and buffered writes with periodic flush (on stop at minimum)
- CLI flags for output path and duration

## Acceptance
- Running tracer produces a non-empty file with valid header and records
- Stats counters advance; timestamps monotonic

## References
- specs/TRACER_SPEC.md (TD-001/002/003, OB-001)
- tracer_backend ring buffer and shared memory implementations
