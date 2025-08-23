# Test Plan — M1 E1 I2 Baseline Hooks

## Goals
Validate deterministic hooks fire and produce index events.

## Cases
- `test_cli`: expect calls to `malloc/free` and I/O functions; non-zero events
- `test_runloop`: events present without crash; call depth varies

## Procedure
1. Run tracer for 3–5s duration.
2. Drain stats show events captured; no drops under nominal load.
3. Persisted file size increases; summarize shows top functions from baseline set.

## Acceptance
- Non-zero events; no crashes; stats increase over time.
