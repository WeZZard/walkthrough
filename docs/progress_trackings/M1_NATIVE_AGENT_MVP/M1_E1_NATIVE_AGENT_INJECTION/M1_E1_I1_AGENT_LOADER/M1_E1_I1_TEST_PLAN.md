# Test Plan — M1 E1 I1 Agent Loader

## Goals
Verify the native agent loads via the QuickJS loader and runs `frida_agent_main`.

## Cases
- Loader creates without error; `on_message` logs contain loader banners.
- Agent prints `[Agent] Installed` line and hook count.
- Failure modes: missing dylib path, permission denied — user-friendly error.

## Procedure
1. Build with `./utils/init_third_parties.sh && cargo build --release`.
2. Run `tracer spawn ./target/debug/test_cli --output traces/run1`.
3. Observe logs; confirm agent hook logs.

## Acceptance
- No crashes; agent hook log visible; process resumes and runs for N seconds.
