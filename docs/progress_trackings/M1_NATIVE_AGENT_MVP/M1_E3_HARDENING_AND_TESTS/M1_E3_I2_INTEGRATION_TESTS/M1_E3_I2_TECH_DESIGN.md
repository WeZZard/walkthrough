# Tech Design — M1 E3 I2 Integration Tests

## Objective
Deterministic integration tests using fixtures to validate end-to-end behavior.

## Design
- Tests execute `test_cli` and `test_runloop` under tracer for short fixed durations
- Assertions on file existence, header, record count range
- CI-like local run uses `cargo test` invoking C-built tests or a Rust harness

## Out of Scope
- Long-run stability/perf tests (tracked separately).
