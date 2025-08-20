# Native Tracer Architecture Analysis & Gaps

## Current Architecture Issues

### 1. **Component Separation Problem**

The current codebase mixes **controller-side** and **agent-side** implementations in the same library (`native_tracer`), causing duplicate symbol conflicts and architectural confusion.

#### Intended Architecture (from FRIDA_NATIVE_AGENT_MACOS_BACKEND.md)

```
┌─────────────────────────────────────────────────────────────┐
│                     Tracer Process (Controller)             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │   frida_controller.c (uses frida-core)              │    │
│  │   - frida_controller_spawn()                        │    │
│  │   - frida_controller_attach()                       │    │
│  │   - frida_controller_inject_library()               │    │
│  └─────────────────────────────────────────────────────┘    │
│                           ▼                                 │
│  ┌─────────────────────────────────────────────────────┐    │
│  │   Shared Memory (Two-Lane Buffer)                   │    │
│  │   - Index Lane (always-on)                          │    │
│  │   - Detail Lane (windowed)                          │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                            │
                     IPC / Injection
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Target Process (Agent)                   │
│  ┌─────────────────────────────────────────────────────┐    │
│  │   frida_agent.c (uses frida-gum)                    │    │
│  │   - gum_init_embedded()                             │    │
│  │   - gum_interceptor_attach() [implements hooks]     │    │
│  │   - Writes events to shared memory                  │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

#### Current Implementation Problems

```
native_tracer library includes:
├── frida_core.c          ✓ Controller bridge (correct)
├── frida_controller.c    ✓ Controller logic (correct)
├── frida_hooks.c         ✗ Agent logic (WRONG PLACE!)
├── frida_hooks_tls.c     ✗ Agent logic (WRONG PLACE!)
└── process_spawn.c       ? Mixed concerns
```

### 2. **Duplicate Symbol Conflicts**

Multiple files implement the same functions:

- `frida_spawn()` - Defined in frida_core.c, frida_hooks.c, frida_hooks_tls.c, process_spawn.c
- `frida_attach()` - Defined in multiple files
- `frida_destroy()` - Defined in multiple files
- etc.

### 3. **Missing Core Functions**

Key functions from the architecture are not implemented:

- `frida_controller_inject_library()` - Critical for agent injection
- `frida_session_inject_library_file_sync()` - Frida-core API for injection
- Proper IPC between controller and agent

### 4. **Function Placement Issues**

`frida_hook_function()` should be in the **agent** (using Gum Interceptor), not the controller:

```c
// WRONG: In controller trying to hook remotely
HookStats frida_hook_function(FridaContext* ctx, const char* function_name);

// RIGHT: Should be in agent, hooking locally
// agent.c (in target process)
void agent_hook_function(const char* function_name) {
    gum_interceptor_attach(interceptor, func_addr, listener, NULL);
}
```

## Test Coverage Analysis

### Tests vs Architecture Alignment

| Test Category | What It Tests | Architecture Alignment |
|--------------|---------------|------------------------|
| `test_minimal_gtest` | Basic functionality | ✓ Generic tests |
| `test_ring_buffer_*` | Shared memory buffers | ✓ Correct concept |
| `test_symbol_cache_*` | Symbol resolution | ✓ Useful for both |
| `test_frida_hooks_gtest` | Hook installation | ✗ Tests wrong layer |
| `test_process_spawn_gtest` | Process spawning | ⚠️ Tests utility, not integration |
| `test_injection_*` | Agent injection | ✗ Missing actual injection |
| `test_controller_*` | Controller logic | ⚠️ Partial coverage |

### Critical Missing Tests

1. **Agent Injection Tests**
   - Test actual library injection into target process
   - Verify agent initialization in target
   - Test IPC between controller and agent

2. **Inter-Process Communication**
   - Shared memory write from agent
   - Shared memory read from controller
   - Event flow across process boundaries

3. **Hook Installation in Agent**
   - Agent-side hook installation (not controller-side)
   - Gum Interceptor usage
   - Event capture and transmission

## Recommended Fixes

### 1. **Separate Libraries**

```cmake
# Controller library (runs in tracer process)
add_library(native_controller STATIC
    frida_controller.c
    frida_core.c  # Public API bridge
    process_spawn.c  # Utilities
)

# Agent library (injected into target)
add_library(frida_agent SHARED
    frida_agent.c
    frida_hooks.c  # Gum-based hooks
    frida_hooks_tls.c  # TLS support
)
```

### 2. **Implement Missing Functions**

```c
// In frida_controller.c
int frida_controller_inject_library(FridaContext* ctx, pid_t pid) {
    FridaSession* session = /* get session for pid */;
    frida_session_inject_library_file_sync(
        session,
        "../lib/libfrida_agent.dylib",  // Library directory, starts from
                                        // the `build/bin` dir.
        "agent_init",  // Entry point
        NULL
    );
}
```

### 3. **Fix Function Placement**

Move hook functions to agent:

```c
// frida_agent.c (runs in target process)
void agent_init(const gchar* data, gint data_size) {
    gum_init_embedded();
    // Set up shared memory
    // Install hooks
    // Start event capture
}
```

### 4. **Update Tests**

Create proper inter-process tests:

```cpp
TEST(AgentInjection, InjectAndCommunicate) {
    // 1. Spawn target process
    // 2. Inject agent library
    // 3. Verify shared memory communication
    // 4. Check event capture
}
```

## Summary

The test failures reveal a fundamental architectural mismatch:

- **Controller and agent code are mixed** in the same library
- **Missing injection mechanism** - no actual agent injection
- **Wrong abstraction level** - controller trying to hook directly instead of through agent
- **Tests don't match intended architecture** - testing in-process hooks instead of inter-process tracing

The fix requires properly separating controller and agent components as described in the architecture document.
