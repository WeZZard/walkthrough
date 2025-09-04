# Interface-Driven Development for ADA

## Overview

ADA adopts a strict interface-driven development model where **all interfaces must compile before any implementation work begins**. This ensures clear contracts between components and prevents implementation-test drift.

## Core Principles

### 1. Interfaces as Law
- Interfaces are immutable contracts once approved
- All disputes resolved by interface definition
- Changes require architect approval and version migration

### 2. Compilable from Day One
Every interface MUST:
- Compile/import successfully immediately
- Include skeleton implementation that compiles
- Define clear performance contracts
- Specify error conditions

### 3. Triple-Check Protocol
When failures occur:
1. **Interface Check** - Does the interface compile?
2. **Implementation Check** - Does implementation match interface?
3. **Test Check** - Does test use interface correctly?

## ADA-Specific Interfaces

### C/C++ (tracer_backend)
```c
// Public API - must compile
typedef struct ThreadRegistry ThreadRegistry;
ThreadRegistry* thread_registry_create(void);
int thread_registry_register(ThreadRegistry* reg, uint64_t tid);

// Performance contract
// Registration: < 1μs
// Fast path: < 10ns
```

### Rust (tracer)
```rust
// Control plane trait - must compile
pub trait TracerControl {
    async fn start_session(&mut self, config: SessionConfig) -> Result<SessionId>;
    async fn inject_tracer(&mut self, process: ProcessHandle) -> Result<()>;
}

// Skeleton implementation
pub struct TracerSkeleton;
impl TracerControl for TracerSkeleton {
    async fn start_session(&mut self, config: SessionConfig) -> Result<SessionId> {
        todo!("Pending implementation")
    }
}
```

### Python (query_engine)
```python
from typing import Protocol

class QueryEngineProtocol(Protocol):
    def analyze(self, events: list[Event], budget: int) -> Analysis: ...
    
# Skeleton implementation
class QueryEngineSkeleton:
    def analyze(self, events: list[Event], budget: int) -> Analysis:
        raise NotImplementedError()
```

## Cross-Language Boundaries

### C++ ↔ Rust
- Use C API with opaque handles
- Shared memory with atomic operations
- No complex types cross boundary

### Rust ↔ Python
- PyO3 bindings for safe interop
- Type conversions clearly defined
- GIL handling explicit

## Development Workflow

### Phase 1: Interface Definition (Architect)
1. Create compilable interface
2. Provide skeleton implementation
3. Define performance requirements
4. Document error cases

### Phase 2: Implementation (Developer)
1. Pull interface from architect
2. Verify interface compiles
3. Implement against interface
4. Cannot modify interface

### Phase 3: Testing (Tester)
1. Generate tests from interface
2. Mock using interface
3. Validate contract compliance
4. Report interface violations

## Performance Contracts

### Registration Path
- **Requirement**: < 1μs
- **Measurement**: `thread_registry_register()` duration
- **Validation**: Performance benchmark in CI

### Fast Path (Event Recording)
- **Requirement**: < 10ns
- **Measurement**: Ring buffer write time
- **Validation**: Continuous benchmarking

### Memory Constraints
- **Hot path**: Zero allocations
- **Steady state**: Bounded memory usage
- **Validation**: Memory profiling tools

## Agent Responsibilities

### Interface Enforcer
- Validates all interfaces compile
- Blocks non-compliant work
- Routes violations to appropriate agent

### Language Integrators
- Analyze build/test failures
- Determine root cause (interface vs impl vs test)
- Provide fix recommendations

### Developers
- Implement to interface exactly
- Report interface issues to architect
- Cannot proceed without compilable interface

## Common Pitfalls

### Interface Drift
**Problem**: Implementation diverges from interface over time
**Solution**: Interface enforcer validates on every build

### Test Assumptions
**Problem**: Tests assume implementation details
**Solution**: Tests only use public interface

### Performance Regression
**Problem**: Implementation violates performance contract
**Solution**: Continuous benchmarking against interface specs

## Benefits for ADA

1. **Parallel Development**: Teams work independently against interfaces
2. **Early Integration**: Skeletons allow early cross-language testing
3. **Clear Ownership**: Interface violations immediately identified
4. **Performance Guarantees**: Contracts enforced from day one
5. **Reduced Debugging**: Triple-check eliminates ambiguity

## Enforcement Mechanisms

1. **Pre-commit hooks**: Interface compilation check
2. **CI/CD gates**: Interface compliance validation
3. **Agent automation**: Automatic routing of violations
4. **Version control**: Interface changes tracked separately

## Migration Strategy

When interfaces must change:
1. Create new version with architect approval
2. Maintain backward compatibility period
3. Update all implementations
4. Update all tests
5. Deprecate old version
6. Remove after migration complete

## Success Metrics

- Interface compilation rate: 100%
- First-time implementation success: > 80%
- Test-implementation mismatch: < 5%
- Performance contract violations: 0%
- Average debug time: < 30 minutes