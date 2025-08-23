# Backlogs — M1 E2 I2 CLI Flags and Shutdown

- Parse `--output` and `--duration` flags (single source of truth across C/Rust CLIs)
- Implement duration timer; graceful stop path triggers: timer expiry, SIGINT/SIGTERM
- Stop drain thread, flush file, detach session, close handles
- Print final stats summary on exit; return non-zero on errors
