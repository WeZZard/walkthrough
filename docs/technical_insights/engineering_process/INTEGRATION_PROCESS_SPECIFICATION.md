# Integration Process Specification: Prevention-First for Mixed Rust/C/C++/Python Repositories

## Problem Statement

When code and documentation are migrated or evolve across repositories, it’s easy for examples, APIs, and build systems to drift apart. Broken examples, stubbed public APIs, and absent cross-language validation are common symptoms in mixed-language (Rust, C/C++, Python) codebases.

This document specifies a prevention-first integration process that keeps the repository self-contained and continuously verifiable across all languages and layers.

## Root Cause Analysis

### Typical Failure Modes

1. No API Contract Validation
   - Examples or docs reference non-existent or unstable APIs
   - Public APIs remain exposed while still using stubs or placeholders
   - No automated policy to block unimplemented APIs from being public

2. Missing Cross-Language Integration Tests
   - Rust, C/C++, and Python layers are built and tested in isolation
   - FFI layers and bindings are not verified end-to-end
   - Failure to prove that high-level calls reach low-level implementations

3. No Example Validation Pipeline
   - Examples compile or run only locally (or not at all)
   - No automated execution of examples for each language
   - Examples accidentally use experimental or private APIs

4. Aspirational Development
   - Documentation and examples get ahead of implementation
   - “Future API” patterns leak into public interfaces
   - No guardrails to keep unstable code out of public surfaces

## Comprehensive Integration Process

### Phase 1: API Contract Enforcement

#### Contract-First Development Rule

```
NO PUBLIC API SHALL BE EXPOSED UNTIL FULLY IMPLEMENTED AND TESTED
```

Implementation patterns per language:

1) Rust

```rust
// APIs in development — not exposed publicly
#[cfg(feature = "unstable")]
pub fn new_experimental_api(...) -> Result<...> { /* real implementation or feature-gated */ }

// Only stable, tested APIs in the public interface
pub fn stable_api(...) -> Result<...> { /* fully implemented */ }
```

- Use feature flags (e.g., `unstable`) to gate experimental items.
- Do not export experimental modules from the root crate unless flagged.

2) C/C++

```c
// public headers: include/...
// experimental APIs are opt-in only
#ifdef ADA_UNSTABLE
int experimental_api(/* ... */);
#endif

// stable APIs exposed by default
int stable_api(/* ... */);
```

- Gate experimental declarations with preprocessor macros (e.g., `ADA_UNSTABLE`).
- Do not install or export experimental headers by default.

3) Python

```python
# python/pkg/__init__.py
import os

# Export only stable symbols by default
__all__ = [
    "stable_api",
]

# Allow opt-in experimental exports via env var or extras
if os.getenv("ADA_UNSTABLE") == "1":
    from .experimental import experimental_api  # noqa: F401
```

- Default `__all__` to stable symbols only; require explicit opt-in for experimental modules.
- Mark experimental modules clearly in docs and avoid using them in examples.

#### API Implementation Gates

- Public APIs must have a full end-to-end implementation.
- Each public API must be covered by:
  - Unit tests at the implementation layer (Rust/C++/Python)
  - Cross-layer integration tests when crossing FFI boundaries
  - At least one working example per language that uses the API

#### Stub/Placeholder Detection (Fail CI)

Block common placeholders in public surfaces:

- Rust: `todo!()`, `unimplemented!()`, `panic!("unimplemented")`, returning sentinel values in public methods
- C/C++: `/* TODO */` in bodies of public functions, `abort()`, `assert(0)`, `return 0;` or `return NULL;` placeholders
- Python: `raise NotImplementedError`, `pass` in public functions/classes, dummy return values

Example CI checks:

```bash
# Rust stub detection
if rg -n "\b(todo!\(|unimplemented!\(|panic!\(\s*\"unimplemented\")" rust/ src/ 2>/dev/null; then
  echo "ERROR: Found Rust stubs in public code"; exit 1; fi

# C/C++ stub detection
if rg -n "\babort\(|assert\(0\)|/\*\s*TODO\s*\*/|return\s+0\s*;|return\s+NULL\s*;" cpp/ c/ include/ src/ 2>/dev/null; then
  echo "ERROR: Found C/C++ stubs in public code"; exit 1; fi

# Python stub detection
if rg -n "raise\s+NotImplementedError|^\s*pass\s*$" python/ 2>/dev/null; then
  echo "ERROR: Found Python stubs in public code"; exit 1; fi
```

Note: Tune directories to match your layout.

### Phase 2: Continuous Integration Pipeline

#### Multi-Language, Multi-Layer Validation

A single pipeline validates all layers and their integration. Use conditional steps so the same pipeline works whether all or only some languages are present.

```yaml
# .github/workflows/integration.yml
name: Mixed-Language Integration Validation

on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4

      # ---------- Rust ----------
      - name: Setup Rust
        if: ${{ hashFiles('**/Cargo.toml') != '' }}
        uses: dtolnay/rust-toolchain@stable

      - name: Rust - Build and Test
        if: ${{ hashFiles('**/Cargo.toml') != '' }}
        run: |
          cargo build --all --all-targets
          cargo test --all --all-features -- --test-threads=1

      - name: Rust - Build Examples
        if: ${{ hashFiles('**/Cargo.toml') != '' && hashFiles('examples/**') != '' }}
        run: |
          cargo build --examples
          # Execute examples with a timeout
          for ex in $(cargo build --examples 2>&1 | rg -o "examples/[^"]+" | awk -F/ '{print $2}' | sort -u); do
            echo "Running Rust example: $ex"
            timeout 30s cargo run --example "$ex" || { echo "Rust example $ex failed"; exit 1; }
          done

      # ---------- C/C++ ----------
      - name: C/C++ - Configure and Build
        if: ${{ hashFiles('**/CMakeLists.txt') != '' }}
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
          cmake --build build --config RelWithDebInfo --parallel

      - name: C/C++ - Tests (ctest)
        if: ${{ hashFiles('**/CMakeLists.txt') != '' }}
        run: |
          ctest --test-dir build --output-on-failure || ctest --test-dir build -R . --output-on-failure

      # ---------- Python ----------
      - name: Setup Python
        if: ${{ hashFiles('**/pyproject.toml') != '' || hashFiles('**/setup.py') != '' }}
        uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Python - Install
        if: ${{ hashFiles('**/pyproject.toml') != '' }}
        run: |
          python -m pip install --upgrade pip
          pip install -e .[dev,test] || pip install -e .

      - name: Python - Lint & Test
        if: ${{ hashFiles('**/pyproject.toml') != '' || hashFiles('**/setup.py') != '' }}
        run: |
          # Lint if available
          ruff --version && ruff check . || true
          # Type check if configured
          mypy --version && mypy . || true
          # Run tests
          pytest -q --maxfail=1

      # ---------- Cross-Layer Integration ----------
      - name: Build Native Components (for FFI)
        if: ${{ hashFiles('**/CMakeLists.txt') != '' }}
        run: |
          cmake -S . -B build -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=ON
          cmake --build build --config RelWithDebInfo --parallel

      - name: Rust <-> C/C++ FFI Tests
        if: ${{ hashFiles('**/Cargo.toml') != '' && hashFiles('**/CMakeLists.txt') != '' }}
        run: |
          cargo test -p ffi_tests -- --test-threads=1 || true

      - name: Python <-> Native/Rust Integration Tests
        if: ${{ hashFiles('**/pyproject.toml') != '' }}
        run: |
          pytest -q -k "integration or ffi" --maxfail=1 || true

      # ---------- Documentation ----------
      - name: Rust Doc Tests
        if: ${{ hashFiles('**/Cargo.toml') != '' }}
        run: cargo test --doc

      - name: Python Doc/Example Tests
        if: ${{ hashFiles('**/pyproject.toml') != '' }}
        run: |
          pytest -q --doctest-glob="*.md" --doctest-modules || true

      # ---------- Stub/Placeholder Detection ----------
      - name: Stub Detection (Multi-Language)
        run: |
          # Rust stubs
          rg -n "\\b(todo!\\(|unimplemented!\\(|panic!\\(\\s*\\\"unimplemented\\\")" rust/ src/ || true
          # C/C++ stubs
          rg -n "\\babort\\(|assert\\(0\\)|/\\*\\s*TODO\\s*\\*/|return\\s+0\\s*;|return\\s+NULL\\s*;" cpp/ c/ include/ src/ || true
          # Python stubs
          rg -n "raise\\s+NotImplementedError|^\\s*pass\\s*$" python/ || true
          # Fail if any found
          if [ "$GITHUB_ACTIONS" = "true" ]; then
            if rg -n "\\b(todo!\\(|unimplemented!\\(|panic!\\(\\s*\\\"unimplemented\\\")" rust/ src/ && exit 1; fi
            if rg -n "\\babort\\(|assert\\(0\\)|/\\*\\s*TODO\\s*\\*/|return\\s+0\\s*;|return\\s+NULL\\s*;" cpp/ c/ include/ src/ && exit 1; fi
            if rg -n "raise\\s+NotImplementedError|^\\s*pass\\s*$" python/ && exit 1; fi
          fi
```

Notes:
- Use `rg` (ripgrep) for fast searches; replace with `grep -R` if unavailable.
- Guard steps using `if:` so the pipeline remains reusable across partial language sets.

#### Documentation Synchronization

- Rust: `cargo test --doc` validates code blocks in Rust documentation.
- Python: `pytest --doctest-glob="*.md" --doctest-modules` validates code examples in docstrings and Markdown.
- For C/C++, consider a small script that extracts fenced `c`, `cpp`, or `c++` blocks and compiles them in CI.

### Phase 3: Development Workflow Integration

#### Pre-Commit Hook (Language-Aware)

```bash
#!/usr/bin/env bash
# .git/hooks/pre-commit
set -euo pipefail

echo "Running integration validation..."

# 1) Block common stub patterns (tune paths as needed)
if command -v rg >/dev/null 2>&1; then
  rg -n "\\b(todo!\\(|unimplemented!\\(|panic!\\(\\s*\\\"unimplemented\\\")" rust/ src/ && { echo "❌ Rust stubs found"; exit 1; } || true
  rg -n "\\babort\\(|assert\\(0\\)|/\\*\\s*TODO\\s*\\*/|return\\s+0\\s*;|return\\s+NULL\\s*;" cpp/ c/ include/ src/ && { echo "❌ C/C++ stubs found"; exit 1; } || true
  rg -n "raise\\s+NotImplementedError|^\\s*pass\\s*$" python/ && { echo "❌ Python stubs found"; exit 1; } || true
else
  echo "ℹ️ Install ripgrep (rg) for faster stub detection"; fi

# 2) Fast builds/tests for each present language
if [ -f Cargo.toml ]; then
  cargo check
  cargo test -q -- --test-threads=1 || { echo "❌ Rust tests failed"; exit 1; }
fi

if [ -f CMakeLists.txt ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
  cmake --build build --config RelWithDebInfo --parallel >/dev/null
  ctest --test-dir build --output-on-failure || { echo "❌ C/C++ tests failed"; exit 1; }
fi

if [ -f pyproject.toml ] || [ -f setup.py ]; then
  python -m pip -q install -U pip >/dev/null 2>&1 || true
  pip -q install -e .[dev,test] >/dev/null 2>&1 || pip -q install -e . >/dev/null 2>&1 || true
  pytest -q --maxfail=1 || { echo "❌ Python tests failed"; exit 1; }
fi

echo "✅ Pre-commit integration validation passed"
```

#### API Development Lifecycle

```
1. [DESIGN]     Write user story + technical spec (cross-language where applicable)
                ↓
2. [PROTOTYPE]  Create minimal working implementation (private/experimental)
                ↓
3. [IMPLEMENT]  Build full implementation + unit tests (per language)
                ↓
4. [INTEGRATE]  Add FFI/binding and cross-layer integration tests
                ↓
5. [VALIDATE]   Create working examples in each language
                ↓
6. [STABILIZE]  Expose API publicly (Rust features off, macros off, Python exports on)
                ↓
7. [MAINTAIN]   Continuous validation in CI (docs, examples, tests)
```

### Phase 4: Repository Structure Enforcement

Adopt a clear, language-aware structure with space for bindings and examples:

```
.
├── rust/                    # Rust crates (workspace optional)
│   ├── Cargo.toml
│   └── crates/...           # one or more crates
├── cpp/ (or c/)             # C/C++ libraries + headers
│   ├── CMakeLists.txt
│   ├── include/
│   └── src/
├── python/                  # Python package(s)
│   ├── pyproject.toml
│   └── pkg/
├── bindings/                # FFI + language bindings (Rust <-> C/C++ <-> Python)
├── examples/
│   ├── rust/
│   ├── cpp/
│   └── python/
├── tests/
│   ├── unit/
│   │   ├── rust/
│   │   ├── cpp/
│   │   └── python/
│   ├── integration/         # Cross-layer integration tests
│   │   ├── rust_cpp.rs
│   │   ├── cpp_rust.cpp
│   │   └── python_native.py
│   └── api/                 # Public API contract tests
└── docs/
```

#### Public API Export Controls

- Rust: Export only stable modules/items from `lib.rs`. Gate experimental via `#[cfg(feature = "unstable")]`.
- C/C++: Public headers expose only stable APIs by default; wrap experimental APIs in `#ifdef ADA_UNSTABLE`.
- Python: Default `__all__` to stable symbols; require explicit env var or extras to import experimental modules.

#### Example Requirements

- Each example:
  - Compiles and runs in CI across supported OS targets
  - Uses only stable, public APIs by default
  - Demonstrates real, working functionality (no mock data)
- Keep examples minimal and focused; prefer one feature per example.

#### Test Structure Enforcement

- Unit tests: Validate behavior per language component
- Integration tests: Prove cross-language paths (e.g., Rust -> C -> Python or Python -> Rust -> C++)
- API contract tests: Assert public surface behavior and backward compatibility expectations

### Phase 5: Metrics and Monitoring

#### Integration Health Dashboard

Track metrics that indicate integration health:

```yaml
metrics:
  api_stability:
    - public_api_changes_per_week
    - unstable_api_usage_in_examples
    - stub_implementation_count

  integration_quality:
    - cross_layer_test_coverage
    - end_to_end_test_success_rate
    - example_execution_success_rate

  language_health:
    - rust_build_success_rate
    - cpp_build_success_rate
    - python_wheel_build_success_rate

  development_velocity:
    - time_from_api_design_to_stable
    - integration_issue_resolution_time
    - broken_build_frequency
```

#### Quality Gates

```yaml
quality_gates:
  - all_examples_compile: 100%
  - all_examples_execute: 100%
  - unit_and_integration_tests_pass: 100%
  - cross_layer_tests_pass: 100%
  - no_stub_implementations: true
  - api_documentation_current: true
```

## Success Criteria

- ✅ Zero broken examples — all examples compile and run successfully across languages
- ✅ Zero fake implementations — all public APIs do real work
- ✅ Zero integration surprises — cross-language compatibility verified continuously
- ✅ Zero API–implementation drift — documentation and examples match implementation
- ✅ Fast feedback loops — developers learn about integration issues within minutes

## Prevention vs. Detection

This process emphasizes prevention:

- Don’t allow broken code to be committed
- Don’t allow public APIs without full implementation
- Don’t allow examples that don’t work
- Don’t allow documentation that doesn’t match code

Rather than detection:

- Finding broken examples after commit
- Discovering API–implementation mismatches late
- Manual, ad-hoc testing to find integration issues
- Post-mortem analysis after integration failures

The goal: Make it operationally difficult to introduce broken public APIs, broken examples, or cross-language integration gaps.
