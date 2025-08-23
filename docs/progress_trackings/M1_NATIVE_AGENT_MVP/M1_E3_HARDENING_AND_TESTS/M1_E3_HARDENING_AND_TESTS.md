# M1 E3: Hardening and Tests

## Goal
Stabilize lifecycle, add drop accounting and metrics, and provide deterministic integration tests on fixtures.

## Deliverables
- Drop reason counters, drain cadence tuning, detach handling
- Integration tests for spawn/attach on fixtures

## Acceptance
- Tests pass with deterministic assertions (ranges)
- Clean shutdown without leaks; stats reported

## References
- specs/TRACER_SPEC.md (BP-003, OB-001, RL-001/002/003)
