---
id: BH-001
title: System Architecture
status: active
source: docs/specs/ARCHITECTURE.md
---

# System Architecture

## Context

**Given:**
- A tracing platform is needed to capture program execution with minimal overhead
- The system must support multiple user interfaces (CLI, MCP Server, Query Engine)
- Both control plane and data plane components must work together efficiently
- Shared memory must be used for high-performance event collection

## Trigger

**When:** The ADA system is deployed and begins tracing a target application

## Outcome

**Then:**
- The system operates as a comprehensive tracing platform with a two-lane architecture
- The User Interface Layer (CLI, MCP Server, Query Engine) provides access to tracing functionality
- The Control Plane (Rust) manages tracer lifecycle, shared memory, event collection, and ATF file writing
- The Data Plane (C/C++) handles process spawning/attachment, function hooking, ring buffer management, and event logging
- Components communicate through well-defined interfaces using shared memory
- The architecture supports selective persistence inspired by aircraft black boxes

## Component Responsibilities

### User Interface Layer
- CLI Tool: Command-line interface for user interaction
- MCP Server: Model Context Protocol implementation for AI agents
- Query Engine: ATF file parsing and analysis

### Control Plane (Rust)
- Lifecycle management of tracing sessions
- Shared memory allocation and management
- Collection of events from shared memory
- Writing ATF (ADA Trace Format) files
- Configuration and policy enforcement

### Data Plane (C/C++)
- Process spawning with injection
- Process attachment via ptrace/task_for_pid
- Native agent injection using Frida
- Function hooking and instrumentation
- Hot-path event logging to shared memory

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interface Layer                    │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐    │
│  │   CLI Tool  │  │  MCP Server  │  │   Query Engine   │    │
│  └──────┬──────┘  └──────┬───────┘  └────────┬─────────┘    │
└─────────┼─────────────────┼──────────────────┼──────────────┘
          │                 │                  │
┌─────────▼─────────────────▼──────────────────▼──────────────┐
│                      Control Plane (Rust)                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                     Tracer Core                      │   │
│  │  - Shared Memory Management                          │   │
│  │  - Event Collection & Aggregation                    │   │
│  │  - ATF File Writing                                  │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────┐
│                   Data Plane (C/C++)                         │
│  ┌──────────────────────────────────────────────────────┐    │
│  │              Tracer Backend (Native Agent)           │    │
│  │  - Process Spawning/Attachment (Frida Core)          │    │
│  │  - Function Hooking (Frida Gum)                      │    │
│  │  - Ring Buffer Management                            │    │
│  │  - Event Logging to Shared Memory                    │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

## Shared Memory Architecture

### Memory Layout
```
┌─────────────────────────────────────┐
│         Control Block (4KB)         │
│  - Magic number                     │
│  - Version                          │
│  - Ring buffer offsets              │
│  - Statistics                       │
├─────────────────────────────────────┤
│     Index Lane Ring Buffer          │
│         (50MB default)              │
│  - Circular buffer                  │
│  - Lock-free write                  │
│  - Multiple reader support          │
├─────────────────────────────────────┤
│     Detail Lane Ring Buffer         │
│         (50MB default)              │
│  - Circular buffer                  │
│  - Triggered writes only            │
│  - Preserved on trigger             │
└─────────────────────────────────────┘
```

### Synchronization Model
- Single Writer: Each process has one agent (writer)
- Multiple Readers: Tracer can read while agent writes
- Lock-Free: Using atomic operations for indices
- Memory Barriers: Proper ordering for cross-CPU visibility

### Addressing Model
- Offsets-only: All pointers in SHM are represented as offsets relative to the mapped arena base
- Per-call materialization: Each reader/writer computes real addresses per call as `addr = base + offset`
- Cross-process consistency: Both agent and controller map the same arena and compute addresses locally

## Edge Cases

### Asynchronous Loader Startup
**Given:** The system needs to inject and load an agent into a target process
**When:** The agent script loading is initiated
**Then:**
- The FridaController performs agent script loading asynchronously using `frida_script_load`
- A unified startup timeout is computed from estimated hook installation workload plus tolerance
- Cancellation enforces the deadline
- The controller gates resume on an explicit readiness signal from the agent
- Hooks are fully installed before execution continues

### Graceful Degradation
**Given:** A component failure occurs during tracing
**When:** The failure is detected
**Then:**
- If detail lane fails → continue with index lane only
- If shared memory full → drop oldest events
- If injection fails → report error, don't crash target
- If symbol resolution fails → use addresses

### Recovery Mechanisms
**Given:** A tracing session encounters a recoverable error
**When:** The error is detected
**Then:**
- Automatic reconnection occurs on agent crash
- Checkpoint/restore is available for long traces
- Partial trace recovery is possible from shared memory

## References

- Original: `docs/specs/ARCHITECTURE.md` (archived source)
- Related: `BH-002-atf-index-format` (ATF Index Format)
- Related: `BH-003-atf-detail-format` (ATF Detail Format)
- Related: `BH-004-function-tracing` (Function Tracing)
- Related: `BH-005-ring-buffer` (Ring Buffer)
