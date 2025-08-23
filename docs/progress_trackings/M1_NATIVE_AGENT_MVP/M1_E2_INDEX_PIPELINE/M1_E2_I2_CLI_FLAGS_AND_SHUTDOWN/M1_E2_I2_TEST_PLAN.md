# Test Plan — M1 E2 I2 CLI Flags and Shutdown

## Goals
Verify CLI flags and orderly shutdown.

## Cases
- `--output` directs file location
- `--duration` stops run automatically and flushes
- SIGINT leads to detach and summary print

## Acceptance
- File appears under requested dir; process exits cleanly; summary printed.
