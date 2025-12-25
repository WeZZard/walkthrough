---
id: BH-004
title: Function Tracing with Full Coverage
status: active
source: docs/specs/TRACER_SPEC.md
---

# Function Tracing with Full Coverage

## Context

**Given:**
- A target application is to be traced for debugging and analysis
- The tracer must capture all function calls and returns with minimal overhead
- Function symbols are available in the main binary and dynamically loaded libraries (DSOs)
- The system operates on macOS (Apple Silicon preferred) as the first MVP platform
- The tracer uses Frida Gum native hooks for instrumentation

## Trigger

**When:** The tracer attaches to or spawns a target process

## Outcome

**Then:**
- Hooks are installed on all resolvable functions in the main binary
- Hooks are installed on all currently loaded DSOs
- Hooks are automatically installed on DSOs loaded during runtime
- For each hooked function, a `FunctionCall` event is emitted on entry
- For each hooked function, a `FunctionReturn` event is emitted on exit
- ABI-relevant argument registers are captured on call (e.g., x0-x7 on ARM64)
- ABI-relevant return-value registers are captured on return (e.g., x0 on ARM64)
- A shallow stack snapshot (default 128 bytes, configurable 0-512 bytes) is captured on entry
- Stack snapshots avoid reading below SP
- Events conform to the ATF schema defined in `BH-002-atf-index-format` and `BH-003-atf-detail-format`
- No user-provided allowlist is required to achieve coverage
- Optional exclusions may exist but are off by default

## Function Identity

Functions are identified using a composite ID system:
- Each loaded module (image) is assigned a small integer `moduleId`
- Each function within a module is assigned a `symbolIndex` (dense ordinal)
- `functionId = (moduleId << 32) | symbolIndex`
- Module UUID → moduleId mapping persisted in trace manifest
- Function tables' hash recorded in manifest

## Event Schema

Events follow the ATF V2 schema with:
- Monotonic timestamps (mach_absolute_time converted to nanoseconds)
- Function ID (64-bit composite)
- Thread ID
- Event kind (CALL=1, RETURN=2, EXCEPTION=3)
- Call depth (stack depth)
- ABI registers (named by architecture: X0, X1, etc.)
- Stack snapshot (0-512 bytes from SP)

## Edge Cases

### Dynamic Module Loading
**Given:** A DSO is loaded during runtime (e.g., via dlopen)
**When:** The module load is detected
**Then:**
- The tracer detects the new module
- Functions from the new module are resolved
- Hooks are installed on the new module's functions
- Duplicate hooks are avoided if the module was already instrumented

### Inline and Non-Exported Functions
**Given:** Some functions are inlined or not exported in the symbol table
**When:** The tracer attempts to resolve functions
**Then:**
- Coverage is best-effort for inline/non-exported sites
- The instruction pointer (IP) is always recorded
- Offline symbolization may be used where needed
- The tracer does not fail if some symbols cannot be resolved

### Reentrancy Protection
**Given:** The tracer instruments allocator or other hot APIs that may be called during logging
**When:** A traced function calls back into tracer logging code
**Then:**
- A reentrancy guard prevents infinite recursion
- The guard is implemented using per-thread flags or TLS
- Recursive calls are detected and skipped to avoid collapse

### Hook Installation Startup Time
**Given:** A target application with ~5k symbols needs to be traced
**When:** Hooks are being installed at startup
**Then:**
- On Apple Silicon dev hardware (M3-class), hook installation for ~5k symbols completes in ≤ 3 seconds for the main binary
- Overall startup including DSOs completes in ≤ 8 seconds under typical app loads
- A unified startup timeout is computed from estimated hook installation workload plus tolerance
- The tracer enforces this deadline and cancels if exceeded

### Asynchronous Agent Loading
**Given:** The agent script needs to be loaded into the target process
**When:** The tracer initiates script loading
**Then:**
- The FridaController uses the asynchronous script load API with `GCancellable`
- A temporary GLib `GMainLoop` is used for the async operation
- The unified startup timeout is enforced via cancellation
- The tracer does not resume the target until the agent signals readiness
- Hooks are fully installed before execution continues
- Cancellation is cleanly handled and surfaced as a timeout-class error

### Stack Snapshot Configuration
**Given:** Different use cases require different stack snapshot sizes
**When:** The tracer is configured for a tracing session
**Then:**
- The default stack window is 128 bytes
- The stack window is configurable from 0 to 512 bytes
- Setting stack window to 0 disables stack capture
- The tracer avoids reading below the stack pointer (SP)

### Performance Overhead Target
**Given:** The tracer is running with default settings (trace-all, 128-byte stack)
**When:** Representative workloads are executed
**Then:**
- The tracer adds ≤ 10% CPU overhead
- Known extremely hot runtime symbols (e.g., objc_msgSend, allocators) may be excluded per documented guidance
- Sustained throughput of ≥ 5 million events/second is achieved across an 8-core dev machine
- Per-core throughput is ≥ 0.6–3.0 million events/second depending on workload
- Median end-to-end encode-to-disk latency is ≤ 25 ms
- 99th percentile latency is ≤ 250 ms

## Configuration

The tracer accepts configuration for:
- Output path (directory for ATF files)
- Stack window size (0-512 bytes, default 128)
- Optional exclude list (modules or functions to skip)
- Sampling rate (future, currently disabled)
- Max depth (call stack depth limit)

## References

- Original: `docs/specs/TRACER_SPEC.md` (archived source)
- Related: `BH-002-atf-index-format` (ATF Index Format - event schema)
- Related: `BH-003-atf-detail-format` (ATF Detail Format - event schema)
- Related: `BH-005-ring-buffer` (Ring Buffer Management)
- Related: `BH-006-backpressure` (Backpressure Handling)
