---
id: US-005
title: Specify project source location
persona: PS-002
status: completed
priority: P1
activity: setup
---

# Specify project source location

**As a** Human Developer
**I want to** tell the debugger where my project's source code is located
**So that** it can link trace events back to the original code and documentation

## Acceptance Criteria

- Source location can be specified via command line or config
- Debugger validates that source path is accessible
- Trace events are correctly linked to source locations
- Multiple source directories can be specified if needed

## References

- Persona: PS-002 (Human Developer)
- Source: docs/user_stories/USER_STORIES.md
