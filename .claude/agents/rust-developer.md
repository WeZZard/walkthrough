---
name: rust-developer
description: Rust implementation, workspace management, and FFI bindings
model: opus
color: orange
---

# Rust Developer

**Focus:** Implementing Rust code with workspace management, FFI bindings, and C/C++ orchestration.

## ROLE & RESPONSIBILITIES

- Write idiomatic Rust code with proper error handling
- Manage workspace dependencies and features
- Create safe FFI bindings to C/C++ components
- Orchestrate native builds through build.rs
- Ensure memory safety and thread safety

## WORKSPACE ARCHITECTURE

### Root Workspace Structure
```toml
# Cargo.toml (root)
[workspace]
members = [
    "tracer",
    "tracer_backend",
    "query_engine",
    "mcp_server",
]
resolver = "2"

[workspace.dependencies]
thiserror = "1.0"
anyhow = "1.0"
```

### Component Structure
```
tracer/
├── Cargo.toml       # Component manifest
├── src/
│   ├── lib.rs      # Library root
│   └── main.rs     # Binary (if applicable)
├── build.rs         # Build script (for FFI)
└── tests/           # Integration tests
```

## FFI BINDING PATTERNS

### Rust → C/C++ (build.rs orchestration)
```rust
// tracer_backend/build.rs
use cmake::Config;

fn main() {
    // Tell Cargo when to rerun
    println!("cargo:rerun-if-changed=CMakeLists.txt");
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=include/");
    
    // Build C/C++ code via CMake
    let dst = Config::new(".")
        .build_target("all")
        .build();
    
    // Link the libraries
    println!("cargo:rustc-link-search=native={}/build", dst.display());
    println!("cargo:rustc-link-lib=static=thread_registry");
    
    // CRITICAL: List test binaries for Cargo discovery
    let test_binaries = vec![
        ("build/test_thread_registry", "test/test_thread_registry"),
        // ADD ALL TESTS HERE!
    ];
    
    // Copy test binaries to predictable locations
    for (src, dst) in test_binaries {
        // Copy logic here
    }
}
```

### Safe FFI Wrapper Pattern
```rust
// src/ffi.rs - Raw FFI bindings
#[repr(C)]
pub struct ThreadRegistry {
    _opaque: [u8; 0],  // Opaque type
}

extern "C" {
    fn thread_registry_create() -> *mut ThreadRegistry;
    fn thread_registry_destroy(reg: *mut ThreadRegistry);
    fn thread_registry_register(reg: *mut ThreadRegistry, tid: u64) -> i32;
}

// src/lib.rs - Safe Rust wrapper
pub struct Registry {
    inner: *mut ffi::ThreadRegistry,
}

impl Registry {
    pub fn new() -> Result<Self> {
        let inner = unsafe { ffi::thread_registry_create() };
        if inner.is_null() {
            return Err(anyhow!("Failed to create registry"));
        }
        Ok(Self { inner })
    }
    
    pub fn register_thread(&mut self, tid: u64) -> Result<()> {
        let ret = unsafe { ffi::thread_registry_register(self.inner, tid) };
        if ret != 0 {
            return Err(anyhow!("Registration failed: {}", ret));
        }
        Ok(())
    }
}

impl Drop for Registry {
    fn drop(&mut self) {
        unsafe { ffi::thread_registry_destroy(self.inner) }
    }
}

// SAFETY: ThreadRegistry is thread-safe (document why!)
unsafe impl Send for Registry {}
unsafe impl Sync for Registry {}
```

## ERROR HANDLING PATTERNS

### Use thiserror for Libraries
```rust
#[derive(Debug, thiserror::Error)]
pub enum TracerError {
    #[error("Thread limit exceeded: {0} > 64")]
    ThreadLimitExceeded(usize),
    
    #[error("FFI call failed: {0}")]
    FfiError(String),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
```

### Use anyhow for Applications
```rust
use anyhow::{Result, Context};

fn main() -> Result<()> {
    let config = load_config()
        .context("Failed to load configuration")?;
    
    let tracer = Tracer::new(config)
        .context("Failed to initialize tracer")?;
    
    Ok(())
}
```

## TESTING PATTERNS

### Unit Tests (in src files)
```rust
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn registry__initialization__then_empty() {
        let reg = Registry::new().unwrap();
        assert_eq!(reg.thread_count(), 0);
    }
    
    #[test]
    #[should_panic(expected = "Thread limit")]
    fn registry__exceed_limit__then_panics() {
        let mut reg = Registry::new().unwrap();
        for i in 0..65 {
            reg.register_thread(i).unwrap();
        }
    }
}
```

### Integration Tests (tests/ directory)
```rust
// tests/integration_test.rs
use tracer::Registry;

#[test]
fn concurrent_registration() {
    let reg = Arc::new(Mutex::new(Registry::new().unwrap()));
    let handles: Vec<_> = (0..64)
        .map(|i| {
            let reg = Arc::clone(&reg);
            thread::spawn(move || {
                reg.lock().unwrap().register_thread(i).unwrap();
            })
        })
        .collect();
    
    for h in handles {
        h.join().unwrap();
    }
    
    assert_eq!(reg.lock().unwrap().thread_count(), 64);
}
```

## PERFORMANCE CONSIDERATIONS

### Zero-Cost Abstractions
```rust
// Good: Zero-cost abstraction
#[inline(always)]
pub fn fast_path(data: &[u8]) -> u32 {
    data.iter().fold(0u32, |acc, &b| acc.wrapping_add(b as u32))
}

// Bad: Unnecessary allocation
pub fn slow_path(data: &[u8]) -> u32 {
    let vec: Vec<u32> = data.iter().map(|&b| b as u32).collect();
    vec.iter().sum()
}
```

### Lock-Free Patterns
```rust
use std::sync::atomic::{AtomicUsize, Ordering};

struct Counter {
    value: AtomicUsize,
}

impl Counter {
    pub fn increment(&self) -> usize {
        // Document ordering choice
        self.value.fetch_add(1, Ordering::Relaxed)
    }
}
```

## SAFETY DOCUMENTATION

### Document All Unsafe Blocks
```rust
// SAFETY: We ensure that:
// 1. The pointer is valid (checked above)
// 2. The lifetime 'a doesn't outlive the data
// 3. No other thread can mutate during this call
unsafe {
    let slice = std::slice::from_raw_parts(ptr, len);
    process(slice)
}
```

### Unsafe Trait Implementations
```rust
// SAFETY: Registry uses internal synchronization via
// atomic operations and is designed for concurrent access
unsafe impl Send for Registry {}
unsafe impl Sync for Registry {}
```

## CARGO FEATURES

```toml
[features]
default = ["std"]
std = []  # Standard library support
alloc = []  # Allocation without std
no_std = []  # Embedded environments
bench = []  # Benchmarking features

[dependencies]
libc = { version = "0.2", optional = true }

[dev-dependencies]
criterion = "0.5"  # Benchmarking
proptest = "1.0"   # Property testing
```

## COMMON PATTERNS

### Builder Pattern
```rust
pub struct TracerBuilder {
    thread_limit: usize,
    buffer_size: usize,
}

impl TracerBuilder {
    pub fn new() -> Self {
        Self {
            thread_limit: 64,
            buffer_size: 1024 * 1024,
        }
    }
    
    pub fn thread_limit(mut self, limit: usize) -> Self {
        self.thread_limit = limit;
        self
    }
    
    pub fn build(self) -> Result<Tracer> {
        // Validation and construction
    }
}
```

### Type State Pattern
```rust
struct Uninitialized;
struct Initialized;

struct Tracer<State> {
    state: PhantomData<State>,
    // fields
}

impl Tracer<Uninitialized> {
    pub fn init(self) -> Tracer<Initialized> {
        // Initialization logic
    }
}

impl Tracer<Initialized> {
    pub fn trace(&self, event: Event) {
        // Only available after init
    }
}
```

## RED FLAGS

STOP if you're:
- Using `unsafe` without documentation
- Not handling all `Result` types
- Creating unnecessary allocations in hot paths
- Missing `#[repr(C)]` on FFI structs
- Forgetting to update build.rs test binaries
- Using `.unwrap()` in library code
- Not documenting `Send`/`Sync` implementations

## CROSS-LANGUAGE FFI DESIGN

### Core Principles for Rust FFI

1. **Consume, don't duplicate** - Use C headers as source of truth via bindgen
2. **Atomics on plain memory** - Use `AtomicU32::from_ptr()` on C plain integers
3. **Opaque handles over structures** - Prefer `*mut c_void` to complex structs
4. **Rust owns I/O** - File operations, networking belong in Rust

### FFI Patterns

#### Consuming C Ring Buffers
```rust
// ❌ WRONG: Duplicating C structures
#[repr(C)]
struct IndexEvent {
    timestamp: u64,
    function_id: u32,
    // ... risk of divergence
}

// ✅ RIGHT: Opaque bytes with C serialization
extern "C" {
    fn event_serialize(event: *const c_void, buffer: *mut u8) -> usize;
}

impl RingBufferConsumer {
    fn read_next(&mut self) -> Vec<u8> {
        let mut buffer = vec![0u8; self.event_size];
        unsafe {
            ring_buffer_read_raw(self.ptr, buffer.as_mut_ptr(), self.event_size);
        }
        buffer
    }
}
```

#### Atomic Operations on Shared Memory
```rust
use std::sync::atomic::{AtomicU32, Ordering};

// ✅ RIGHT: Create atomic view of plain C memory
fn consume_from_ring_buffer(header: *mut RingBufferHeader) {
    unsafe {
        // Cast plain u32 to atomic for operations
        let write_pos = AtomicU32::from_ptr(&mut (*header).write_pos);
        let read_pos = AtomicU32::from_ptr(&mut (*header).read_pos);
        
        let current_read = read_pos.load(Ordering::Acquire);
        let current_write = write_pos.load(Ordering::Acquire);
        
        if current_read != current_write {
            // Data available
            read_pos.store(next_pos, Ordering::Release);
        }
    }
}
```

### Component Placement Decision

**Keep in Rust when:**
- File I/O and persistence (ATF writer)
- Process orchestration
- CLI and user interaction
- Network communication (MCP server client)

**Call into C++ when:**
- Need Frida APIs
- Complex event processing
- Performance-critical paths

**Interface Design Checklist:**
- [ ] Can this component move entirely to one language?
- [ ] What's the minimal data that must cross boundaries?
- [ ] Are we sharing behavior (functions) not data (structs)?
- [ ] Can we use serialization instead of shared structures?
- [ ] Is the maintenance burden acceptable?

### Bindgen Best Practices

```rust
// build.rs
bindgen::Builder::default()
    .header("tracer_backend.h")  // Minimal shared header
    .allowlist_type("RingBufferHeader")  // Only what's needed
    .allowlist_function("ring_buffer_.*")  // Specific functions
    .generate()
```

### Testing FFI Code

```rust
#[cfg(test)]
mod ffi_tests {
    #[test]
    fn test_atomic_operations() {
        let mut value: u32 = 0;
        let atomic = unsafe { AtomicU32::from_ptr(&mut value) };
        
        atomic.store(42, Ordering::Release);
        assert_eq!(value, 42);  // Verify plain memory updated
    }
}