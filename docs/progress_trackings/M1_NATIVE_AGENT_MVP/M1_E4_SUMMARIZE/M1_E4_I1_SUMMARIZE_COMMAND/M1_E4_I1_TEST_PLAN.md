# Test Plan — M1 E4 I1 Summarize Command

## Goals
Validate summarize reads index file and prints expected aggregates quickly.

## Cases
- Invalid path → clear error and non-zero exit
- Valid trace → totals > 0; unique threads ≥ 1; top function IDs list non-empty
- Performance: runs in < 1s on sample trace

## Procedure
1. Use trace generated in integration tests
2. Run `tracer summarize <trace.idx>`
3. Parse output (or return code) to assert fields present and sane

## Acceptance
- Output contains expected sections; command exits 0; time under 1s.
