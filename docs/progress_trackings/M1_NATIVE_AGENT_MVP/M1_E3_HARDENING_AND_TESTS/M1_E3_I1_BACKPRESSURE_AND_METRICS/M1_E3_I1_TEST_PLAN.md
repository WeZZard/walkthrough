# Test Plan — M1 E3 I1 Backpressure and Metrics

## Goals
Validate drop accounting under induced pressure and visibility of metrics.

## Cases
- Induce overflow: temporarily reduce ring size or throttle drain; verify `events_dropped` increments
- Verify `events_captured`, `bytes_written`, `drain_cycles` increase over time
- Metrics print cadence ~5s; values are sane and non-decreasing

## Procedure
1. Run tracer against `test_cli` with drain sleep increased (dev knob) or smaller ring
2. Capture logs/metrics for ~10s
3. Assert drops > 0 when overflow induced; otherwise 0 under nominal

## Acceptance
- Drop counters behave as expected; no crashes; metrics visible.
