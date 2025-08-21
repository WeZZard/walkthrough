# Tracer Backend - Proof of Concept

This is a proof-of-concept implementation of the ADA tracer backend based on the architecture design in `docs/tech_designs/NATIVE_TRACER_BACKEND_ARCHITECTURE.md`.

## Architecture Overview

The tracer backend implements a **two-lane flight recorder architecture**:

1. **Controller Process** (frida-core based)
   - Manages process lifecycle (spawn/attach/detach)
   - Injects agent into target process
   - Drains events from shared memory
   - Writes events to storage

2. **Agent Library** (frida-gum based)
   - Injected into target process
   - Installs hooks on functions
   - Writes events to shared memory rings
   - Minimal overhead instrumentation

3. **Shared Memory IPC**
   - Lock-free SPSC ring buffers
   - Zero-copy event transfer
   - Two lanes: Index (always-on) and Detail (windowed)

## Building

### Prerequisites

1. Initialize Frida SDKs:

```bash
cd ..
./utils/init_third_parties.sh
```

2. Install build tools:

```bash
# macOS
brew install cmake

# Linux
sudo apt-get install cmake build-essential
```

### Build Steps

```bash
cd tracer-backend

# Build everything via Cargo (this runs CMake internally via build.rs)
cargo build --release

# The build will automatically generate compile_commands.json for IDE support
# Location: target/debug/build/tracer-backend-*/out/build/compile_commands.json
# A symlink is created at: target/compile_commands.json

# Run the tracer
cargo run --bin tracer -- spawn ./target/debug/test_cli
```

### IDE Integration

The build automatically generates `compile_commands.json` for C/C++ IDE support:

1. **VSCode**: The `.vscode/c_cpp_properties.json` is pre-configured to use `${workspaceFolder}/target/compile_commands.json`
2. **CLion**: Point to `target/compile_commands.json` in CMake settings
3. **Neovim/Vim**: LSP servers will automatically find it at `target/compile_commands.json`

To merge compile_commands.json from multiple components:
```bash
python utils/merge_compile_commands.py
```

This will build:

- `libtracer_backend` - Rust library wrapping the native components
- `libtracer_controller.a` - Controller library (built via CMake in build.rs)
- `libfrida_agent.dylib` - Agent library (built via CMake in build.rs)
- `tracer` - Main executable (Rust binary)
- `test_cli` - CLI test program (built via CMake)
- `test_runloop` - RunLoop test program (built via CMake)

## Usage

### Spawn and trace a new process

```bash
# Using cargo run
cargo run --bin tracer -- spawn ./target/debug/test_cli --wait

# Or using the built binary
./target/release/tracer spawn ./target/debug/test_cli --wait
```

### Trace a RunLoop-based program

```bash
cargo run --bin tracer -- spawn ./target/debug/test_runloop
```

### Attach to existing process

```bash
cargo run --bin tracer -- attach <PID>
```

### Options

- `--output <dir>` - Specify output directory for traces (default: ./traces)

## Testing

### Run all tests

```bash
# Run Rust tests
cargo test

# Build and run C tests (if needed for debugging)
cargo build --release
./target/debug/test_shared_memory
./target/debug/test_ring_buffer
./target/debug/test_integration
```

## Key Components

### `frida_controller.h/c`

- Main controller managing Frida interaction
- Process lifecycle management
- Hook installation coordination

### `frida_agent.c`

- Injected agent running in target process
- Gum-based function interception
- Event capture and writing

### `shared_memory.h/c`

- POSIX shared memory abstraction
- Cross-process memory sharing

### `ring_buffer.h/c`

- Lock-free SPSC ring buffer
- Atomic operations for thread safety
- Batch read/write support

### Test Programs

- **test_cli.c** - Simple CLI program with various function calls
- **test_runloop.c** - macOS RunLoop-based program with timers and dispatch queues

## Event Format

### Index Event (32 bytes)

```c
typedef struct {
    uint64_t timestamp;      // Monotonic timestamp
    uint64_t function_id;    // Module ID + symbol index
    uint32_t thread_id;      // Thread identifier
    uint32_t event_kind;     // CALL/RETURN/EXCEPTION
    uint32_t call_depth;     // Call stack depth
} IndexEvent;
```

### Detail Event (512 bytes)

```c
typedef struct {
    // Same as index event
    uint64_t timestamp;
    uint64_t function_id;
    uint32_t thread_id;
    uint32_t event_kind;
    
    // Additional context
    uint64_t x_regs[8];      // ARM64 registers
    uint64_t lr, fp, sp;     // Special registers
    uint8_t stack[128];      // Stack snapshot
} DetailEvent;
```

## Performance

Target metrics (from architecture spec):

- Hook overhead: <1%
- Memory usage: ~68MB
- Event throughput: >1M events/sec
- Drain latency: <10ms

## Limitations (POC)

This is a proof-of-concept with some limitations:

1. **Simplified hook script** - Currently hooks all exports, production would be selective
2. **No symbol resolution** - Function IDs are sequential, not based on actual symbols
3. **No flight recorder triggers** - Detail lane is not yet triggered by events
4. **Basic drain thread** - Simple periodic draining, not optimized
5. **No protobuf output** - Raw binary format instead of ATF protobuf

## Next Steps

To productionize this POC:

1. Implement proper symbol resolution and caching
2. Add flight recorder trigger system
3. Integrate protobuf serialization for ATF format
4. Optimize drain thread with batching
5. Add configuration file support
6. Implement query engine integration
7. Add performance benchmarks

## Troubleshooting

### "Failed to spawn process"

- Check if the target binary exists and is executable
- On macOS, may need Developer Tools permission

### "Failed to attach"

- Verify the PID exists
- Check permissions (may need sudo for system processes)
- On macOS with SIP, cannot attach to system binaries

### "Shared memory error"

- Check if previous run left stale segments: `ipcs -m`
- Clean up with: `ipcrm -M <key>`

### Build errors with Frida

- Ensure Frida SDKs are initialized: `../utils/init_third_parties.sh`
- Check platform compatibility (macOS arm64, Linux x86_64, etc)

## Contributing

This POC follows the architecture defined in:

- `docs/tech_designs/NATIVE_TRACER_BACKEND_ARCHITECTURE.md`
- `docs/specs/TRACER_SPEC.md`

When making changes, ensure alignment with these specifications.
