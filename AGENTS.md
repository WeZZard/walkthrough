# Repository Guidelines

## Project Overview

ADA (AI Agent Debugging Architecture) is a high-performance tracing system designed for debugging AI agents with minimal overhead and token-budget-aware analysis. The system provides a dual-lane flight recorder architecture:

- **Index Lane**: Always-on lightweight event capture
- **Detail Lane**: Selective persistence of rich debugging data  
- **Token-Budget-Aware**: Optimized for LLM context window constraints

## Project Structure & Modules

Core components and critical directories:

```plaintext
project-root/
├── Cargo.toml                     # CRITICAL: Root workspace manifest - orchestrates ALL builds
├── docs/
│   ├── business/                  # Business analysis
│   ├── user_stories/              # User stories  
│   ├── specs/                     # Technical specifications
│   ├── technical_insights/        # Technical insights
│   │   ├── ada/                   # Technical insights for ADA
│   │   └── engineering_process/   # Technical insights for engineering_process
│   └── progress_trackings/        # CRITICAL FOR PLANNERS: Development workflow artifacts
│       └── M{X}_{MILESTONE_NAME}/           # Milestone folders (X = milestone number)
│           ├── M{X}_{MILESTONE_NAME}.md     # Milestone target document
│           └── M{X}_E{Y}_{EPIC_NAME}/       # Epic folders (Y = epic number)
│               ├── M{X}_E{Y}_{EPIC_NAME}.md # Epic target document
│               └── M{X}_E{Y}_I{Z}_{ITERATION_NAME}/ # Iteration folders (Z = iteration number)
│                   ├── M{X}_E{Y}_I{Z}_TECH_DESIGN.md
│                   ├── M{X}_E{Y}_I{Z}_TEST_PLAN.md
│                   └── M{X}_E{Y}_I{Z}_BACKLOGS.md
│
├── tracer/                       # Rust tracer (control plane)
│   └── Cargo.toml                # Component manifest
├── tracer_backend/               # C/C++ backend (data plane) - MODULAR STRUCTURE
│   ├── Cargo.toml                # CRITICAL: Rust manifest that orchestrates CMake
│   ├── build.rs                  # CRITICAL: Invokes CMake via cmake crate
│   ├── CMakeLists.txt            # Root CMake - includes subdirectories
│   ├── include/tracer_backend/   # PUBLIC headers only (opaque types)
│   │   └── {module}/             # {module} public API
│   ├── src/                      # PRIVATE implementation
│   │   ├── CMakeLists.txt        # Source modules build
│   │   └── {module}/             # {module} implementation
│   │       ├── CMakeLists.txt    # {module} build
│   │       ├── *.c/cpp           # Implementation files
│   │       └── *_private.h       # Private headers
│   └── tests/                    # Test files
│       ├── CMakeLists.txt        # Test modules build
│       ├── *.h                   # Shared headers in tests
│       ├── bench/                # Benchmarks
│       │   └── {module}/             # {module} benchmark
│       │       ├── CMakeLists.txt    # {module} benchmark build
│       │       ├── *.c/cpp           # Tests files
│       │       └── *.h               # Private headers for {module} benchmark
│       ├── unit/                 # Unit tests
│       │   └── {module}/             # {module} unit tests
│       │       ├── CMakeLists.txt    # {module} unit tests build
│       │       ├── *.c/cpp           # Tests files
│       │       └── *.h               # Private headers for {module} unit tests
│       └── integration/          # Integration tests
│           └── {module}/             # {module} unit tests
│               ├── CMakeLists.txt    # {module} unit tests build
│               ├── *.c/cpp           # Tests files
│               └── *.h               # Private headers for {module} integration tests
├── query_engine/                 # Python query engine
│   ├── Cargo.toml                # Rust manifest for Python binding
│   └── pyproject.toml            # Python config (built via maturin)
├── mcp_server/                   # Python MCP server
│   ├── Cargo.toml                # Rust manifest (if using maturin)
│   └── pyproject.toml            # Python config
├── utils/                        # Engineering efficiency scripts
├── third_parties/               # Frida SDK and dependencies
└── target/                      # Build outputs (git-ignored)
```

## Technology Stack

### Core Components
- **tracer**: Rust control plane for trace management
- **tracer_backend**: High-performance C/C++ data plane with modular architecture
- **query_engine**: Python-based token-budget-aware analysis with PyO3 Rust bindings
- **mcp_server**: Model Context Protocol interface for AI agent integration

### Build System
- **Cargo workspace**: Orchestrates all builds from root Cargo.toml
- **CMake**: Builds C/C++ components via tracer_backend/build.rs
- **Maturin**: Builds Python extensions with Rust bindings
- **GoogleTest**: C++ testing framework
- **Pytest**: Python testing framework

## Build, Test, and Dev Commands

### Essential Commands
```bash
# Build all components
cargo build --release

# Test all crates
cargo test --all

# Generate coverage reports
./utils/run_coverage.sh

# Install quality gate hooks (MANDATORY)
./utils/install_hooks.sh

# Initialize third-party dependencies (Frida SDK)
./utils/init_third_parties.sh
```

### Development Commands
```bash
# Query engine local development
maturin develop -m query_engine/Cargo.toml
cd query_engine && pytest -q

# MCP server development
pip install -e mcp_server[dev]
cd mcp_server && pytest -q

# Coverage dashboard
./utils/run_coverage.sh

# Individual component testing
cargo test -p tracer
cargo test -p tracer_backend
cargo test -p query_engine
```

## Coding Style & Naming

### Rust
- **Formatting**: `rustfmt` defaults
- **Linting**: `cargo clippy -- -D warnings`
- **Files/Modules**: `snake_case`
- **Error Handling**: Use `anyhow`/`thiserror` for explicit errors

### Python  
- **Formatting**: `black` (88 columns)
- **Linting**: `ruff`
- **Type Checking**: `mypy` (no untyped defs)
- **Packages/Files**: `snake_case`
- **Dependencies**: numpy, pandas, orjson for query_engine

### C/C++
- **Formatting**: `clang-format` (LLVM style)
- **Files**: `snake_case.{c,cpp,h}`
- **Testing**: GoogleTest with behavioral naming: `component__case__then_expected`
- **Headers**: Public headers in `include/tracer_backend/`, private headers as `*_private.h`

### General Principles
- Keep functions small and focused
- Use explicit error handling
- Follow existing patterns in each component
- Maintain modular architecture in tracer_backend

## Testing Guidelines

### Rust
- **Unit tests**: In `src` with `#[cfg(test)]`
- **Integration tests**: In `tests/` directory
- **Run with**: `cargo test`
- **Coverage**: 100% on changed lines required

### C++
- **Framework**: GoogleTest in `tracer_backend/tests/`
- **Test types**: unit/, integration/, bench/
- **Naming**: Behavioral names like `component__case__then_expected`
- **Structure**: Modular organization matching src/ structure

### Python
- **Framework**: `pytest` with files `tests/test_*.py`
- **Coverage**: 100% on changed lines required
- **Configuration**: Defined in pyproject.toml

### Quality Requirements
- **Build Success**: 100% (all components must build)
- **Test Success**: 100% (all tests must pass)  
- **Coverage**: 100% on changed lines
- **Integration Score**: 100/100
- **No Bypass**: Quality gates enforced via pre-commit hooks

## Security & Platform Notes

### macOS Development (CRITICAL)
- **Apple Developer Certificate ($99/year) REQUIRED** - NO EXCEPTIONS
- Required for ALL testing and development
- Frida requires proper code signing for dynamic instrumentation
- Tests will fail without proper signing, even locally
- See `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md` for setup

### Linux Development
- May require ptrace capabilities
- See platform-specific setup in Getting Started guide

### General Security
- Frida SDK integration for dynamic instrumentation
- Code signing required for all development builds
- Proper entitlements required for macOS tracing

## Development Workflow

### Quality Gate Enforcement
- **Pre-commit hooks**: Block commits that fail critical gates
- **Post-commit hooks**: Generate coverage reports in background
- **No bypassing**: `--no-verify` explicitly disallowed per CLAUDE.md
- **Critical Gates**:
  - Build must succeed
  - All tests must pass
  - No incomplete implementations (todo!/assert(0)/etc)
  - Changed code must have ≥80% coverage (100% preferred)

### Documentation Structure
```
docs/
├── business/                # Business analysis
├── user_stories/           # User requirements  
├── specs/                  # Technical specifications
├── technical_insights/     # Deep technical analysis
└── progress_trackings/     # Development workflow artifacts
```

### Commit & Pull Request Guidelines
- **Conventional Commits**: `feat:`, `fix:`, `refactor(scope):`, `test:`, `docs:`, `chore:`
- **PR Requirements**:
  - Describe changes and link issues
  - Include appropriate tests
  - Pass CI with 100% quality gates
  - Follow existing patterns and conventions
  - Maintain 100% coverage on changed lines

## Performance Considerations

### tracer_backend (C/C++)
- High-performance data plane with minimal overhead
- Modular architecture for selective compilation
- Thread-safe design with proper synchronization
- Memory-mapped files for efficient I/O

### tracer (Rust)
- Async/await for concurrent operations
- Lock-free data structures where possible
- Memory-efficient trace storage
- Cross-platform compatibility

### query_engine (Python)
- Token-budget-aware analysis algorithms
- Efficient data structures (pandas, numpy)
- Rust backend for performance-critical operations
- Caching strategies for repeated queries

## Dependencies & Third Parties

### Required Dependencies
- **Frida SDK**: Dynamic instrumentation (download via init_third_parties.sh)
- **GoogleTest**: C++ testing framework (fetched automatically)
- **PyO3**: Python-Rust bindings
- **Tokio**: Async runtime for Rust

### Workspace Dependencies
- Common Rust dependencies managed in root Cargo.toml
- CMake-based C++ dependency management
- Python dependencies in pyproject.toml files
- Version pinning for reproducible builds

## Troubleshooting

### Common Issues
1. **macOS signing failures**: Ensure Apple Developer Certificate is properly configured
2. **Frida SDK missing**: Run `./utils/init_third_parties.sh`
3. **CMake errors**: Check CMake version (3.20+ required)
4. **Python import errors**: Ensure maturin development install
5. **Coverage failures**: Check coverage tool installation

### Support
- Documentation: See docs/ directory structure
- Issues: Create GitHub issues for problems
- Setup: Follow docs/GETTING_STARTED.md carefully