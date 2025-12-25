---
id: US-008
title: Detect protected processes
persona: PS-001
status: completed
priority: P1
activity: environment
---

# Detect protected processes

**As an** AI Agent
**I want to** detect system binaries and platform-protected processes before attempting attachment
**So that** I can inform users immediately about platform limitations without wasting time on impossible operations

## Acceptance Criteria

- System binaries are identified before trace attempt
- Protected process detection is platform-aware
- Detection happens quickly without side effects
- Clear information about why process cannot be traced

## References

- Persona: PS-001 (AI Agent)
- Source: docs/user_stories/USER_STORIES.md
