# M1 E1: Native Agent Injection

## Goal
Reliably load the native agent (.dylib) into the target via Frida and invoke `frida_agent_main`, then install baseline hooks.

## Deliverables
- QuickJS loader script created and loaded via frida-core
- Controller computes absolute agent path and handles errors/lifecycle
- Agent runs and reports installed hooks

## Acceptance
- On `spawn test_cli`, loader logs appear; agent prints hook installation summary
- No double-resume; detach cleans up without crashes

## References
- specs/TRACER_SPEC.md (§4, RL-002)
- tracer_backend/src/frida_controller.c, frida_agent.c
