# Test Plan — M1 E5 I1 Getting Started

## Goals
Ensure a new user can follow GETTING_STARTED to build, trace, and summarize.

## Cases
- Build succeeds after running init_third_parties
- `spawn test_cli` run produces a trace; summarize prints expected aggregates
- Troubleshooting covers common failures (permissions, missing SDK)

## Acceptance
- Steps reproduce on a clean macOS dev machine; no missing info.
