---
id: US-017
title: Arm tracing triggers with pre/post roll
persona: PS-001
status: completed
priority: P2
activity: execution
---

# Arm tracing triggers with pre/post roll

**As an** AI Agent
**I want to** arm tracing triggers (by symbol, duration threshold, crash/signal, time window) and set pre/post roll for selective persistence without restarting the target
**So that** I can capture full-detail windows around key events with minimal perturbation

## Acceptance Criteria

- Triggers can be armed by symbol name or address
- Duration and signal-based triggers are supported
- Pre/post roll capture windows are configurable
- Triggers activate without restarting target process

## References

- Persona: PS-001 (AI Agent)
- Source: docs/user_stories/USER_STORIES.md
