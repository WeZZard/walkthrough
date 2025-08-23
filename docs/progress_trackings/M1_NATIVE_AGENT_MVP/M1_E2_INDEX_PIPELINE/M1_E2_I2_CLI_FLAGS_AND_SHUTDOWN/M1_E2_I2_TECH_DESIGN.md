# Tech Design — M1 E2 I2 CLI Flags and Shutdown

## Objective
Expose output path and duration flags, ensure clean shutdown with final flush.

## Design
- CLI: `spawn <path> [-- args...]`, `attach <pid>`, `--output <dir>`, `--duration <sec>`
- On signal or duration elapsed: stop drain thread, flush file, detach

## Out of Scope
- Advanced config, triggers, sampling.
