---
id: US-025
title: Startup timeout with expert override
persona: PS-002
status: completed
priority: P2
activity: execution
---

# Startup timeout with expert override

**As a** Human Developer
**I want to** have a single, predictable startup timeout policy that waits for hooks to finish installing, with an optional expert override in milliseconds (--startup-timeout)
**So that** the tool serves the debugging task purpose rather than local preferences

## Acceptance Criteria

- Default timeout policy is well-documented
- Timeout waits for hook installation completion
- Expert override flag accepts millisecond value
- Override is clearly marked as advanced option

## References

- Persona: PS-002 (Human Developer)
- Source: docs/user_stories/USER_STORIES.md
