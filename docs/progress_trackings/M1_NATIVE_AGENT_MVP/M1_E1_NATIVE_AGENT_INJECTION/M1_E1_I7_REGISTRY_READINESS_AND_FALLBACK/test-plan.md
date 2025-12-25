---
id: M1_E1_I7-tests
iteration: M1_E1_I7
---
# M1_E1_I7 Test Plan: Registry Readiness, Warm‑Up, and Fallback

## Coverage Map
- Unit (agent): state machine transitions (ready, warm‑up, steady, stall, recovery), epoch change re‑attach.
- Unit (controller): heartbeat tick, mode transitions, ready flag.
- Integration: controller + agent lifecycle; warm‑up to steady; induced drain stall; automatic fallback + recovery.
- Performance: warm‑up overhead bounded; steady‑state equals baseline.

## Unit Tests (Agent)
1) startup__registry_not_ready__then_global_only
2) ready_flag__set__then_dual_write_until_heartbeat_healthy
3) heartbeat__stall__then_dual_write_then_global_only
4) heartbeat__resume__then_back_to_per_thread_only
5) epoch_change__then_re_attach_and_re_warm

## Unit Tests (Controller)
6) init__then_registry_ready_flag_set
7) drain_loop__then_heartbeat_monotonic
8) capture_rate_drop__then_request_dual_write_mode

## Integration Tests
9) attach__warmup_to_steady__then_events_captured
- Spawn/attach; assert registry_ready; observe heartbeat; verify agent reaches per_thread_only.

10) induced_stall__then_fallback_and_recovery
- Pause drain thread; verify agent enters dual_write → global_only; resume drain; verify recovery to per_thread_only.

11) epoch_roll__then_agent_re_warm
- Re-initialize registry (increment epoch); ensure agent re-attaches and returns to steady state.

## Performance
12) warmup_window__under_target
- Ensure warm‑up dual‑write window ≤ configured budget (e.g., < 2s).

13) steady_state__throughput_equal_baseline
- Per‑thread steady‑state matches baseline (no penalty).

## Acceptance Criteria
- Readiness + heartbeat visible and correct.
- Agent reaches steady per‑thread state under healthy controller within target window.
- Under induced stall, agent falls back and recovers automatically; no data loss on transitions.
- Performance budgets met.

