# Interface-Driven Development for ADA

## Overview

This document describes the interface-driven development approach implemented for the ADA project, ensuring clean boundaries between components and languages.

## Core Principles

### 1. Interfaces Compile First (Gate 0)

Before any implementation begins, all interfaces must:
- Compile successfully in their respective languages
- Link with skeleton implementations
- Pass basic validation tests

### 2. Language Separation

Each language owns specific responsibilities:
- **C++**: Low-level system interaction, Frida hooks, hot paths
- **Rust**: File I/O, process orchestration, control plane
- **Python**: Data analysis, user interfaces, ML integration

### 3. Minimal Cross-Language Surface

Share the absolute minimum across language boundaries:
- Plain memory with atomic operations (not complex types)
- Opaque handles with C API functions (not structures)
- Stable interfaces separate from evolving implementations

## Interface Locations

### C/C++ Interfaces
- **Location**: `tracer_backend/include/tracer_backend/interfaces/`
- **Key Files**:
  - `thread_registry_interface.h` - Thread registry with SPSC queues
- **Features**:
  - Opaque types with C API
  - Performance contracts as constants
  - Cache-line aware memory layouts

### Rust Interfaces
- **Location**: `tracer/src/lib.rs`
- **Key Traits**:
  - `TracerControl` - Main control plane interface
  - `EventPersistence` - Event storage abstraction
  - `DrainService` - Event draining service
  - `BackendFFI` - C++ backend wrapper
- **Features**:
  - Async-first design
  - Clear ownership boundaries
  - Zero-copy where possible

### Python Interfaces
- **Location**: 
  - `query_engine/src/interfaces.py` - Query engine protocols
  - `mcp_server/src/interfaces.py` - MCP server protocols
- **Key Protocols**:
  - `ATFReader/Writer` - Trace format I/O
  - `QueryEngine` - Token-aware analysis
  - `MCPServer` - Model Context Protocol
- **Features**:
  - Protocol-based type safety
  - Token budget awareness
  - Streaming support

## Cross-Language Contracts

### C++ ↔ Rust FFI

```c
// C++ owns concrete type
typedef struct ThreadRegistry ThreadRegistry;

// Rust sees only opaque pointer
ThreadRegistry* thread_registry_init(void* memory, size_t size);
```

**Shared Memory**:
```c
// Plain memory with atomics
typedef struct {
    uint32_t write_pos;  // Atomic access
    uint32_t read_pos;   // Atomic access
} RingBufferHeader;
```

### Rust ↔ Python (PyO3)

```rust
// Rust trait
pub trait QueryEngine {
    async fn execute_query(&mut self, request: QueryRequest) -> QueryResult;
}
```

```python
# Python protocol
class QueryEngine(ABC):
    @abstractmethod
    async def execute_query(self, request: QueryRequest) -> QueryResult:
        ...
```

## Performance Contracts

Interfaces include explicit performance requirements:

```c
// In thread_registry_interface.h
#define THREAD_REGISTRATION_MAX_NS 1000  // <1μs requirement
#define LANE_ACCESS_MAX_NS 10            // <10ns fast path
```

These are enforced by benchmarks during testing.

## Validation Process

### 1. Interface Compilation Test
```bash
# C/C++
test_interface_compilation

# Rust  
cargo build

# Python
python3 tests/test_interfaces.py
```

### 2. Skeleton Implementation
Each interface has a minimal skeleton that:
- Returns appropriate error values
- Allows early integration testing
- Located in `src/interfaces/` for C/C++

### 3. Cross-Language Validation
FFI boundaries are tested with:
- Type size assertions
- Memory layout validation
- Atomic operation compatibility

## Benefits

1. **Early Error Detection**: Interface issues found before implementation
2. **Parallel Development**: Teams can work independently with clear contracts
3. **Maintainability**: Changes to interfaces are explicit and controlled
4. **Performance**: Contracts enforced from the start
5. **Documentation**: Interfaces serve as living documentation

## Guidelines for Developers

### When Adding New Interfaces

1. **Define the interface first** in the appropriate location
2. **Create a skeleton implementation** that compiles
3. **Add validation tests** for the interface
4. **Document performance requirements** as constants
5. **Get interface reviewed** before implementation

### When Modifying Interfaces

1. **Update all affected languages** simultaneously
2. **Ensure backward compatibility** or version appropriately
3. **Update skeleton implementations**
4. **Run all interface tests** before committing

### Interface Design Rules

- **Minimize shared surface** - Only share what's absolutely necessary
- **Use opaque handles** - Hide implementation details
- **Prefer functions over data** - Share behavior, not structures
- **Document atomicity** - Be explicit about memory ordering
- **Consider cache lines** - Align for performance

## Example: Thread Registry Interface

The thread registry demonstrates all principles:

1. **Opaque Type**: `ThreadRegistry*` hides implementation
2. **C API**: Functions for all operations
3. **Performance Contract**: `<1μs` registration time
4. **Atomic Operations**: Documented memory ordering
5. **Cache Alignment**: SPSC queues on separate cache lines

```c
// Opaque handle
typedef struct ThreadRegistry ThreadRegistry;

// Performance contract
#define THREAD_REGISTRATION_MAX_NS 1000

// C API with clear semantics
ThreadRegistry* thread_registry_init(void* memory, size_t size);
ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, 
                                       uintptr_t thread_id);
```

## Conclusion

Interface-driven development ensures ADA's components remain modular, performant, and maintainable across language boundaries. By defining and validating interfaces first, we catch integration issues early and enable parallel development.