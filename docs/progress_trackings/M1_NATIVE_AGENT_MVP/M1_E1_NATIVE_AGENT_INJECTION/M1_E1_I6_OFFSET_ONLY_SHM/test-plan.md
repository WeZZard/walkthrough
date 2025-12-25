---
id: M1_E1_I6-tests
iteration: M1_E1_I6
---
# M1_E1_I6 Test Plan: Offset-Only SHM

## Test Coverage Map
- Unit: SHM layout invariants, offsets-only registration, accessor materialization.
- Integration: Attach and read, producer/consumer correctness, concurrency.
- Performance: Hot-path latency after warmup; registration cost; throughput.

## Unit Tests
1) registration__offsets_only__then_no_absolute_pointers
- Register a thread and inspect SHM:
  - `index_layout_off > 0`, `detail_layout_off > 0`
  - All former pointer fields are zero/null (compile-time removed or zeroed)
  - `ring_descs[i].offset > 0`, `ring_descs[i].bytes > 0`

2) raw_access__compute_per_call__then_valid
- For event write/read paths, compute `layout` and `ring_addr` on each call and use raw ring helpers:
  - Expect successful write/read with no persistent handles.
  - Optionally assert via instrumentation that no per-lane address state is retained between calls.

3) spsc_queues__offsets_layout__then_enqueue_dequeue_ok
- Use `submit_queue`/`free_queue` arrays via accessors; verify FIFO order.

## Integration Tests
4) attach__offsets_only__then_event_round_trip
- Initialize registry in one process, attach in another:
  - Materialize addresses from offsets-only SHM
  - Write and read events through rings; verify payloads

5) concurrent_registration__unique_slots__then_consistent_indices
- Multiple writer threads register; controller attaches and validates slot indices and thread_ids.

## Performance Tests
6) fast_path__post_warm__then_ns_budget_met
- Measure average latency for per-call compute + raw ring write; must meet <10ns target (allow small CI headroom if needed).

7) registration_cost__then_under_1us
- Repeat registration and measure; must meet <1μs avg.

## Acceptance Criteria
- [ ] No absolute pointers present in SHM after registration
- [ ] All accessors function via materialization
- [ ] Attach + read/write works cross-process
- [ ] Performance budgets met

Execution Notes (current run):
- SHM now contains only offsets (no `ring_ptrs`, `rb_handles`, `ring_memory_base`).
- Accessors switched to per-call materialization; validated by unit tests.
- Attach/read/write verified in integration (`test_thread_registry_integration`) using header-only helpers.
- Unit perf checks remain within budgets in CI-like conditions.
