# Technology Selection Document — Agentic Debugger / Agentic Tracer

## 1. Goal

Build an "agentic debugger" capable of collecting **per-function call data**:

- Registers relevant to the platform ABI
- Shallow stack memory snapshot
- Function entry/return timestamps
- Address-to-symbol mapping (symbolization)
- Type/parameter mapping via DWARF or other debug info

Use this to enable **AI agents** to narrate and reason about a program's runtime behavior.

---

## 2. Primary Use Cases

| Use Case | Target OS | Notes |
|----------|-----------|-------|
| First-party app debugging | Linux, macOS (dev builds) | Developers tracing their own code for AI-assisted debugging |
| In-house closed-source libs | Linux, macOS (dev builds) | Enterprise code bases with proprietary components |
| Cloud / container services | Linux | Agent runs alongside services in staging/prod |
| Reverse engineering | Linux, macOS (partial) | Niche, high-value; limited to targets without hardening or with bypasses |

---

## 3. Platform Feasibility

### 3.1 Linux

✅ **Viable for full-process coverage**

- Use **eBPF uprobes/uretprobes** for safe in-kernel function entry/exit trampolines.
- Capture ABI registers + shallow stack in BPF program; stream via ring buffer.
- Track `dlopen`/`dlclose` via `_dl_debug_state` to attach to new DSOs.
- Post-process events offline for narration.
- Optionally integrate hardware tracing (Intel PT / ARM ETM) for whole-program control flow with low overhead.

### 3.2 macOS

⚠️ **Viable for developer-owned targets; not viable for hardened third-party apps**

- Use **Frida Interceptor** inline hooks for targeted functions; capture regs + shallow stack.
- Requires:
  - `com.apple.security.get-task-allow`
  - `com.apple.security.cs.disable-library-validation`
  - `com.apple.security.cs.allow-jit`
- Works reliably on:
  - Dev/debug builds
  - Apps signed with matching entitlements
- Blocked or fragile on:
  - Mac App Store apps
  - Apple system apps
  - Hardened third-party binaries

---

## 4. Why Not "Trace Everything" via Inline Hook

Inline hooking **can** provide precise regs + stack for every call, but:

- **Overhead**: per-call patch → trampoline → context save → callback is costly for hot paths.
- **Stability**: fragile instruction relocation, multi-thread patching races, tail call/exception paths.
- **OS security**: blocked by hardening on macOS for non-owned targets.
- **Maintainability**: sensitive to CPU/OS updates, PAC on arm64e.
- **Coverage gaps**: compiler inlining removes many call sites.

---

## 5. Recommended Technical Approach

### 5.1 Linux Backend (Primary)

- **Mechanism**: eBPF uprobes/uretprobes (libbpf CO-RE)
- **Scope control**: exports-only, allowlists, regex, depth caps
- **Event schema**:
  - PID/TID/timestamp
  - IP / return address
  - Kind (entry/return)
  - Arch
  - ABI-relevant regs
  - Stack length + bytes
- **Post-processing**: DWARF + ABI map → arg narration
- **Dynamic module tracking**: `_dl_debug_state` hook

### 5.2 macOS Backend (Developer Opt-In)

- **Mechanism**: Frida Interceptor + return-thunk
- **Performance tuning**:
  - CModule hot path + shared-memory ring buffer
  - Minimal JS per event (no per-event `send()`)
  - Rate limiting / sampling
- **Scope control**: targeted functions/modules
- **Deployment**: dev builds with entitlements; no MAS/system app tracing

---

## 6. Market Considerations

| Segment | Size | Viability | Notes |
|---------|------|-----------|-------|
| First-party dev/debug | Large | High | Core adoption target |
| Linux server/cloud | Large | High | Primary prod environment |
| macOS dev | Medium | Medium | Dev-time tracing only |
| Third-party RE/security | Small (niche) | Medium | High-value, limited market |

---

## 7. MVP Plan

1. **Shared Event Schema** (`TraceEvent`)
   - Fixed-size struct for cross-platform compatibility
2. **Linux MVP**
   - Attach uprobes to 5–10 functions in 1 binary
   - Capture regs + stack; write to file
   - Narrator parses DWARF + ABI map → human-readable
3. **macOS MVP**
   - Hook same functions in dev build via Frida
   - Same schema, same narrator
4. **Validation**
   - Golden test programs with known args/types
   - Metrics: accuracy, overhead, event rate

---

## 8. Key Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| OS hardening on macOS | Limit scope to dev builds; focus on Linux for broad coverage |
| Event volume overload | Scope control, rate limiting, sampling |
| DWARF gaps | Fallback to ABI position + heuristics |
| Hot path overhead | CModule + ring buffer; avoid heavy per-event logic |
| Relocation fragility (macOS) | Use Frida’s proven Gum engine; test on each arch/OS release |

---
