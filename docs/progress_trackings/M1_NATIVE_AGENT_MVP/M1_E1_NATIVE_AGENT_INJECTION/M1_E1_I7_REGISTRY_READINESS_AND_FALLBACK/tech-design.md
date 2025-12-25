---
id: M1_E1_I7-design
iteration: M1_E1_I7
---
# M1_E1_I7 Technical Design: Registry Readiness, Warm‑Up, and Fallback (Always‑On Per‑Thread Rings)

## Overview
Elevate per‑thread rings to the default data path by adding a robust, observable coordination mechanism between controller and agent:

- Readiness + heartbeat signals in the shared ControlBlock (IPC, not env vars).
- Agent warm‑up using dual‑write (per‑thread + global) until drain is healthy, then steady per‑thread.
- Health‑checked fallback to avoid drops when drain stalls; automatic recovery.
- Version/epoch checks for safe re‑attachment.

This design builds on I6 (offsets‑only SHM + raw header helpers) and makes the per‑thread data plane production‑reliable.

## Goals
- Default to per‑thread rings; zero startup loss; bounded warm‑up window.
- Automatic fallback/recovery without human intervention.
- Clear observability (counters + logs) for operations and debugging.

## Non‑Goals
- No dependence on environment variables for runtime synchronization.
- No breaking changes to existing global ring compatibility path (kept during rollout window).

## IPC Contract (ControlBlock Extensions)
- `registry_ready: u32` — 0→1 when controller creates registry and starts drain thread.
- `drain_heartbeat_ns: u64` — Monotonic timestamp updated each drain loop.
- `registry_epoch: u32` — Incremented on re‑init; agents re‑attach on change.
- `registry_version: u32` — Schema version (for future forwards compatibility).
- `registry_mode: u32` — Controller’s requested mode:
  - 0 = global_only
  - 1 = dual_write (warm‑up / degraded)
  - 2 = per_thread_only (steady state)

Memory ordering:
- Controller: writes with release semantics.
- Agent: reads with acquire semantics.

## Agent State Machine
```
global_only  --(registry_ready=1)-->  dual_write --(heartbeat healthy N×)--> per_thread_only
    ^                                         |                          |           |
    |                                         |--(stall S ms)-----------|           |
    |-------------------------------------------------------(stall S ms)------------|
```

- Startup: `global_only` until `registry_ready == 1`.
- Warm‑up: `dual_write` until `drain_heartbeat_ns` advances for a grace interval (e.g., ≥ 500–2000 ms).
- Steady state: `per_thread_only` (raw header writes to per‑thread rings only).
- Stall fallback: If heartbeat stops advancing for `S` ms, enter `dual_write`; if still stalled, drop to `global_only`. Recover back to `per_thread_only` when heartbeat resumes.
- Epoch change: If `registry_epoch` changes, clear attachments/materialization and re‑enter warm‑up.
- Overflow handling: If per‑thread ring full, increment overflow counter; optionally mirror to global to avoid drops under bursts.

## Controller Responsibilities
- On init: set `registry_ready = 1` after registry + drain thread are live.
- Each drain loop: update `drain_heartbeat_ns`.
- Drive `registry_mode` transitions: start in `dual_write` after ready, elevate to `per_thread_only` when healthy; request fallback if capture drops significantly.
- Continue draining global rings during rollout window for compatibility.

## Observability
- Agent counters: `events_written_pt`, `events_mirrored_global`, `fallback_activations`, `pt_overflows`, `last_heartbeat_seen_ns`.
- Controller counters: `events_captured_pt`, `events_captured_global`, `bytes_written`, `drain_cycles`, `last_heartbeat_ns`.
- Logs: mode transitions, epoch/version changes, first successful registry attach/materialization.

## Rollout Plan
1. Ship IPC fields + controller heartbeat + counters behind feature flag; leave default path unchanged (dual‑write disabled).
2. Enable warm‑up + fallback by default; maintain global drain for a deprecation window.
3. Make per‑thread only the default steady state; keep global drain as a hidden ops fallback.

## Risks & Mitigations
- Stall on attach or drain startup — mitigated by readiness + heartbeat + warm‑up dual‑write.
- Schema drift — controlled via `registry_version` and `registry_epoch` with re‑attach.
- Performance cost of dual‑write — bounded window; raw header write cost is small compared to event copy.

## Dependencies
- I6 completed (offsets‑only SHM + raw helpers + controller drain via headers).

## Acceptance Criteria
- Agent switches from `global_only` → `dual_write` → `per_thread_only` within the target warm‑up window when controller is healthy.
- Under simulated drain stall, agent enters `dual_write` (then `global_only` if persistent) and recovers automatically.
- Controller heartbeat visible and monotonically increasing; registry_ready set correctly.
- No data loss during startup and under transient drain stalls (validated by integration tests).

