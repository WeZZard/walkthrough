# Coverage Helper

Unified coverage collection tool for Rust, C/C++, and Python code using LLVM tools.

## Quick Start

```bash
# Build the helper
cargo build -p coverage_helper

# Run full coverage workflow
./target/debug/coverage_helper full --format lcov

# Or use individual commands
./target/debug/coverage_helper clean    # Clean old coverage data
./target/debug/coverage_helper collect  # Collect coverage from test runs
./target/debug/coverage_helper report   # Generate coverage report
```

## Key Feature: Unified LLVM Tools for All Languages

This tool leverages the fact that both Rust and C/C++ can use the same LLVM coverage infrastructure. Instead of using different tools for different languages (gcov for C/C++, tarpaulin for Rust), we use LLVM's tools for everything.

## Documentation

All documentation is inline in the source code. Run `cargo doc --open -p coverage_helper` or read `src/main.rs` directly for:

- Detailed usage examples
- How LLVM coverage works
- C/C++ setup instructions
- Tool discovery logic
- Troubleshooting guide

## Benefits

- **Single toolchain**: Same LLVM tools for Rust and C/C++
- **Consistent format**: All languages produce LCOV
- **Auto-discovery**: Finds LLVM tools even when not in PATH (common on macOS)
- **No extra deps**: Uses tools from Rust toolchain or Homebrew