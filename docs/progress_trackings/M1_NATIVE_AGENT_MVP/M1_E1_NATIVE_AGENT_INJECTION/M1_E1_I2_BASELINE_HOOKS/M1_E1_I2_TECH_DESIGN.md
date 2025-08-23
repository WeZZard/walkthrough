# Tech Design — M1 E1 I2 Baseline Hooks

## Objective
Install a deterministic set of hooks from the native agent and emit index events into the ring buffer with TLS reentrancy guard.

## Design
- Use existing `frida_agent.c`:
  - TLS for reentrancy; thread id; call depth
  - Hook exports: `open, close, read, write, malloc, free, memcpy, memset`
  - Emit `IndexEvent` on enter/leave; capture minimal ABI registers and optional 128B stack window (guarded)
- Keep small set in MVP; reserve full coverage to a later epic.

## Out of Scope
- Dynamic module tracking; symbol table scanning; key-symbol lane.
