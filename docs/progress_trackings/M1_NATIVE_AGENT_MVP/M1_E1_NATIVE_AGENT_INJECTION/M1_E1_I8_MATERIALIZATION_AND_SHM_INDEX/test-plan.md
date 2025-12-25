---
id: M1_E1_I8-tests
iteration: M1_E1_I8
---
# M1_E1_I8 Test Plan: Address Materialization & SHM Index Registration

## Unit Tests
1) directory__published__then_indices_stable
- Controller writes directory with one entry (registry arena); agent reads back.
- Names and sizes match; `count == 1`.

2) attach__map_entries__then_build_local_bases
- Map `entries[0]` and store base[0]; verify non-null and size matches.

3) accessor__index_offset_materialization__then_handle_valid
- Using `(shm_idx=0, offset)`, materialize address and attach ring; writes/reads succeed.

## Integration Tests
4) cross_process__consistent_indices__then_round_trip
- Controller creates directory; agent attaches.
- Verify both see same indices and can round-trip events using offsets-only SHM.

5) mismatch__name_or_size__then_attach_error
- Corrupt directory entry; attach must fail with explicit error code/log.

## Performance
6) materialization_once__then_hot_path_unchanged
- Confirm no regression in cached access; swap/registration unchanged from I6.

## Acceptance Criteria
- [ ] Canonical directory read/write works and matches across processes
- [ ] Accessors work via (index, offset) materialization
- [ ] Robust attach error handling for mismatches
