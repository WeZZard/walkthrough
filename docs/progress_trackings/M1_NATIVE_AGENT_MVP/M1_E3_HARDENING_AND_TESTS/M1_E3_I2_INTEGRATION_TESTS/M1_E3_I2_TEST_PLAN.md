# Test Plan — M1 E3 I2 Integration Tests

## Goals
End-to-end verification on fixtures with deterministic assertions and artifacts.

## Cases
- `spawn ./target/debug/test_cli --duration 3 --output traces/run1` produces a valid `.idx` file
- `attach <pid>` smoke: attach, install hooks, detach cleanly within 3s
- Header magic/record size valid; record count within expected range

## Procedure
1. Build fixtures and tracer
2. Run spawn case; then run summarize; capture outputs
3. For attach, launch `test_cli` separately, then run tracer attach for short duration

## Acceptance
- All assertions pass; no leaks/crashes; artifacts present under output dir.
