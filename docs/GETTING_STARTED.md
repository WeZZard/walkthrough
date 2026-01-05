# Getting Started with ADA

This guide provides comprehensive setup and build instructions for developers working with the ADA tracing system.

## Prerequisites

### Required Tools

| Tool | Version | Installation |
|------|---------|-------------|
| Rust | stable (1.89.0+) | `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \| sh` |
| Clang | 15.0+ | `brew install llvm` (macOS) / `apt install clang` (Linux) |
| Python | 3.9+ | `brew install python` / `apt install python3` |
| CMake | 3.20+ | `brew install cmake` / `apt install cmake` |

### Platform-Specific Requirements

#### macOS: Apple Developer Certificate (MANDATORY)

**⚠️ CRITICAL**: You MUST have an Apple Developer account ($99/year) to develop ADA on macOS.

**Why this is required:**
- Remote development (SSH) is now standard in 2025
- Frida requires proper code signing for dynamic instrumentation  
- Tests will fail without proper signing, even locally
- No workarounds or exceptions

**Setup steps:**

1. **Purchase Apple Developer membership**
   - Go to: https://developer.apple.com/programs/
   - Cost: $99/year (tax deductible for professionals)
   - Processing time: Usually instant, sometimes 48 hours

2. **Create Developer ID certificate**
   ```bash
   # In Xcode:
   # Xcode → Settings → Accounts → Manage Certificates
   # → Click "+" → Developer ID Application
   ```

3. **Configure your environment**
   ```bash
   # Add to ~/.zshrc or ~/.bashrc:
   export APPLE_DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)"
   
   # Find your exact certificate name:
   security find-identity -v -p codesigning
   ```

4. **Verify setup**
   ```bash
   # This should succeed:
   echo 'int main(){}' > test.c && clang test.c -o test
   ./utils/sign_binary.sh test
   rm test test.c
   ```

**Without this certificate, you will see:**
```
ERROR: APPLE_DEVELOPER_ID not configured
Tests cannot run without proper code signing
Quality gate will fail
```

**For CI/CD Setup**: See `docs/engineering_efficiency/CI_CERTIFICATE_SETUP.md` for configuring certificates in GitHub Actions and other CI systems.

#### Linux: No Special Requirements

Linux uses ptrace for tracing, which may require:
- Running with `sudo` for some operations
- Adjusting `/proc/sys/kernel/yama/ptrace_scope` if restricted

#### Windows: Future Support

Windows support is planned for future releases.

### Coverage and Quality Tools

```bash
# Coverage tools
brew install lcov                    # LCOV merging and HTML generation
pip install diff-cover coverage-lcov # Changed-line coverage checking
cargo install cargo-llvm-cov         # Rust coverage
rustup component add llvm-tools-preview # LLVM tools for coverage

# Quality tools
cargo install clippy rustfmt         # Rust linting and formatting
pip install black ruff pytest        # Python formatting and testing
```

## Initial Setup

### 1. Clone the Repository

```bash
git clone https://github.com/your-org/ada.git
cd ada
```

### 2. Initialize Third-Party Dependencies

```bash
# Download and extract Frida SDKs (required for tracer-backend)
./utils/init_third_parties.sh
```

This script will:
- Download platform-specific Frida Core and Gum SDKs
- Extract them to `third_parties/frida-core/` and `third_parties/frida-gum/`
- Verify the installation

### 3. Install Git Hooks

```bash
# Install pre-commit and post-commit hooks for quality enforcement
./utils/install_hooks.sh
```

## Building the Project

### Full Build (All Components)

```bash
# Build all components in release mode
cargo build --release

# Build with coverage instrumentation
cargo build --release --features tracer_backend/coverage,query_engine/coverage
```

### Component-Specific Builds

```bash
# Build only the tracer
cd tracer && cargo build --release

# Build only the backend (C/C++)
cd tracer_backend && cargo build --release

# Build query engine
cargo build -p query_engine --release
```

## Running Tests

### All Tests

```bash
# Run all tests across all components
cargo test --all

# Run tests with coverage enabled
cargo test --all --features tracer_backend/coverage,query_engine/coverage
```

### Component-Specific Tests

```bash
# Rust tests
cargo test -p tracer

# C/C++ tests (Google Test)
./target/release/tracer_backend/test/test_ring_buffer
./target/release/tracer_backend/test/test_shared_memory

# Query engine tests
cargo test -p query_engine
```

## Coverage Collection

### Generate Coverage Report

```bash
# Run the unified coverage workflow
./utils/run_coverage.sh

# This will:
# 1. Run all tests with coverage instrumentation
# 2. Generate per-language LCOV files
# 3. Merge into target/coverage_report/merged.lcov
# 4. Create HTML report at target/coverage_report/html/index.html
```

### Check Coverage for Changed Files

```bash
# Ensure 100% coverage on changed lines (runs in pre-commit hook)
diff-cover target/coverage_report/merged.lcov \
    --compare-branch=origin/main \
    --fail-under=100
```

## Quality Gates

### Manual Quality Check

```bash
# Run full quality gate checks
./utils/metrics/integration_quality.sh full

# Quick check (no coverage)
./utils/metrics/integration_quality.sh score-only

# Incremental check for staged changes
./utils/metrics/integration_quality.sh incremental
```

### Requirements

All code must meet these requirements (enforced by pre-commit hooks):

1. **Build Success**: 100% - All components must build
2. **Test Success**: 100% - All tests must pass
3. **Coverage**: 100% - All changed lines must have test coverage
4. **No Incomplete Code**: No `todo!()`, `unimplemented!()`, `assert(0)`

## Running the Tracer

### Basic Usage

```bash
# Spawn a new process with tracing
./target/release/tracer spawn -- /path/to/target/binary

# Attach to existing process
./target/release/tracer attach <pid>

# With custom output file
./target/release/tracer spawn -o trace.atf -- /path/to/target/binary
```

### Environment Variables

```bash
# Set coverage profile output location
export LLVM_PROFILE_FILE="target/coverage/prof-%p-%m.profraw"

# Enable debug logging
export RUST_LOG=debug

# Set shared memory size (bytes)
export ADA_SHM_SIZE=104857600  # 100MB
```

## Troubleshooting

### Common Issues

#### "llvm-tools not found"
```bash
rustup component add llvm-tools-preview
# Or install via Homebrew
brew install llvm
```

#### "cmake not found" (macOS)
```bash
brew install cmake
```

#### "Frida SDK not found"
```bash
# Re-run initialization
./utils/init_third_parties.sh
```

#### Coverage report missing
```bash
# Ensure coverage tools are installed
brew install lcov
pip install diff-cover coverage-lcov
cargo install cargo-llvm-cov
```

#### Permission denied on macOS
```bash
# Grant terminal permissions in System Preferences
# Security & Privacy → Privacy → Developer Tools
```

## Platform-Specific Notes

### macOS
- Requires codesigning for attaching to processes
- May need to disable SIP for system process tracing
- Use `brew` for tool installation

### Linux
- Requires `CAP_SYS_PTRACE` capability or root for attaching
- Use distribution package manager for tools
- May need to adjust `ptrace_scope`

### Windows
- Not currently supported
- Use WSL2 for development

## Next Steps

1. Read the [System Architecture](tech-specs/behaviors/BH-001-system-architecture.md)
2. Review [Engineering Standards](engineering_efficiency/ENGINEERING_STANDARDS.md)
3. Check [Progress Tracking](progress_trackings/) for current development status
4. See component-specific docs in each component's `docs/` directory

## Getting Help

- Check [Troubleshooting](#troubleshooting) section
- Review existing issues on GitHub
- Consult CLAUDE.md for AI-assisted development
- Run tests with `--nocapture` for detailed output