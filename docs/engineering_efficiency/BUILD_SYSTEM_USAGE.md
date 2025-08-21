# Build System Usage Guide

## Overview

The entire project is orchestrated through Cargo, maintaining the principle that **Cargo is the single driver for all builds**.

## Building the Project

```bash
# Build everything (all components including C/C++ and examples)
cargo build --release

# Build specific component
cargo build --package tracer_backend
cargo build --package third_parties
```

## Running Tests

All tests are integrated with Cargo's test framework:

```bash
# Run all tests
cargo test

# Run tests for specific package
cargo test --package tracer_backend

# Run specific test
cargo test test_ring_buffer

# Run tests with single thread (for shared memory tests)
cargo test -- --test-threads=1

# Run ignored tests (those requiring elevated permissions)
cargo test -- --ignored
```

## Predictable Output Locations

After building, all artifacts are available at predictable paths without hash suffixes:

### Tracer Backend Components
- **Binaries**: `/target/{debug,release}/tracer_backend/bin/`
  - `tracer_poc` - Main tracer executable
- **Tests**: `/target/{debug,release}/tracer_backend/test/`
  - `test_[variant_tests_names]`
  - `test_[variant_test_fixture_names]`
- **Libraries**: `/target/{debug,release}/tracer_backend/lib/`
  - `libtracer_controller.a`
  - `libtracer_utils.a`
  - `libfrida_agent.dylib`

### Frida Examples
- **Location**: `/target/{debug,release}/frida_examples/`
  - `frida_gum_example` - Function hooking demonstration
  - `frida_core_example` - Process attachment demonstration

## Running Frida Examples

Since Frida examples are C programs built through CMake (orchestrated by Cargo), run them directly:

```bash
# Run Frida Gum example (function hooking)
./target/release/frida_examples/frida_gum_example

# Run Frida Core example (requires PID)
./target/release/frida_examples/frida_core_example $(pgrep Safari)
```

## Debugging

### With LLDB

```bash
# Debug a test
lldb ./target/release/tracer_backend/test/test_ring_buffer
(lldb) run

# Debug Frida example
lldb ./target/release/frida_examples/frida_gum_example
(lldb) run
```

### With Rust's built-in debugging

```bash
# Run tests with backtrace
RUST_BACKTRACE=1 cargo test

# Run with full backtrace
RUST_BACKTRACE=full cargo test
```

## IDE Integration

The build system generates `compile_commands.json` for C/C++ IDE support:
- Location: `/target/compile_commands.json` (symlink)
- Actual file: `/target/{debug,release}/build/tracer_backend-{hash}/out/build/compile_commands.json`

Configure your IDE to use `/target/compile_commands.json` for C/C++ IntelliSense.

## Clean Build

```bash
# Clean all build artifacts
cargo clean

# Rebuild from scratch
cargo clean && cargo build --release
```

## Important Notes

1. **Never run CMake directly** - Always use `cargo build`
2. **Never run maturin directly** - Python components will be built through Cargo
3. **All builds go through Cargo** - This ensures consistency and reproducibility
4. **Predictable paths** - All artifacts are copied to hash-free locations for easy access

## Troubleshooting

### Frida SDKs Not Found
```bash
# Initialize Frida SDKs first
./utils/init_third_parties.sh
```

### Shared Memory Conflicts in Tests
```bash
# Run tests with single thread
cargo test -- --test-threads=1
```

### Permission Errors
```bash
# Some tests require elevated permissions
sudo cargo test -- --ignored
```