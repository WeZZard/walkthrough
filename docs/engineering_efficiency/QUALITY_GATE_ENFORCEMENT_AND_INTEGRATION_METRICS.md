
# Quality Gate Enforcement and Integration Metrics: Mixed Rust/C/C++/Python Repositories

The repository enforces mandatory quality standards through automated systems with an incremental approach to committing code. This document is repository-independent and designed for mixed-language environments (Rust, C/C++, Python).

## Philosophy

Quality gates should:

1. Block critical issues (broken builds, failing tests)
2. Track but not block aspirational metrics (e.g., total coverage targets)
3. Focus on incremental improvement (new/modified code quality > legacy codebase)
4. Ensure repository quality can only increase, never decrease

## Two-Tier Quality System

### Critical Metrics (Blocking — must be 100%)

- Build Success: 100% across all present languages (Rust, C/C++, Python)
- Test Success: 100% (unit, integration, examples) across languages
- No Regressions: 100% (new failures vs baseline are blocked)
- API Implementation Completeness: 100% (no public stubs/placeholders)
- Cross-Layer Compatibility: 100% (Rust ↔ C/C++ FFI; Python ↔ Native/Rust)

### Informational Metrics (Non-blocking — tracked for improvement)

- Total Test Coverage (composite across languages)
- Documentation Coverage and Accuracy (doctests, code examples compile/run)
- Static Analysis and Linting Health (clippy/ruff/mypy/cppcheck)
- Dependency and Security Health (cargo audit/pip-audit)
- Packaging Health (Python sdist/wheel builds; C/C++ shared libs; Rust crates docs)
- Performance Benchmarks (baselined; trend tracked)

## Incremental Coverage Approach (Latest Strategy)

### For New/Modified Code

Requirement: 100% test coverage for changed files/units per language.

- Rust: Compute coverage for changed crates/modules using tarpaulin or grcov
- C/C++: Use gcovr/llvm-cov with ctest to focus on changed translation units
- Python: Use pytest-cov with path filters for changed modules

Example workflow (unified script recommended):

```bash
# Check coverage for changes only (multi-language aware)
./utils/metrics/integration_quality.sh --incremental
```

This ensures:

- New features include tests
- Bug fixes include regression tests
- Refactoring maintains/raises coverage
- Code quality improves incrementally with each commit

### For Existing Code (Legacy)

- Do not require 100% coverage for untouched code
- Add tests when modifying legacy code
- Track total coverage trend (should increase over time)
- Focus on incremental quality improvement

## Automated Enforcement Points

1) Pre-commit Hook: Blocks commits that fail critical quality gates

```bash
# Install mandatory hooks
./utils/install_hooks.sh
```

2) CI (e.g., GitHub Actions): Enforces quality on PRs and pushes

- Runs on pushes/PRs to main/develop
- Blocks merges that fail critical gates
- Publishes reports for informational metrics

## Quality Metrics Framework

A unified integration quality script (utils/metrics/integration_quality.sh) should evaluate:

- Build Success Rate: Must be 100% (Rust + C/C++ + Python + Examples)
- Test Success Rate: Must be 100% (unit + integration + examples)
- Test Coverage: ≥80% for new/modified code; total coverage tracked
- API Implementation Completeness: Detects stubs/placeholders in public APIs
  - Rust: todo!/unimplemented!/panic("unimplemented")
  - C/C++: abort/assert(0)/TODO placeholders/return 0 or NULL shortcuts
  - Python: raise NotImplementedError/pass in public functions/classes
- Cross-Layer Compatibility: Validates Rust ↔ C/C++ FFI and Python ↔ Native/Rust integration tests
- Documentation Accuracy: Verifies doctests and code examples compile/run
- Static Analysis: Lint/type-check status (clippy/ruff/mypy/cppcheck)
- Security/Dependency Health: cargo audit, pip-audit (non-blocking by default)

## Enforcement Rules

ALLOWED:

- Code that maintains or improves critical metrics
- Tests that pass 100%
- Documentation that remains accurate
- Incremental improvements without forcing 100% total coverage
- New/modified code with ≥80% coverage (per language)

BLOCKED:

- Any code reducing critical metrics below 100%
- Ignoring failing tests (must fix root cause)
- Bypassing checks (e.g., --no-verify)
- “Temporary” quality compromises in critical paths
- New/modified code with <80% coverage

## Quality Gate Commands

Before committing (choose one):

```bash
# Quick critical checks only (builds/tests/stubs)
./utils/metrics/integration_quality.sh --score-only

# Incremental coverage for changes (recommended)
./utils/metrics/integration_quality.sh --incremental

# Full multi-language quality report with details
./utils/metrics/integration_quality.sh --full

# Install mandatory hooks
./utils/install_hooks.sh
```

If you don’t use a unified script yet, run per-language commands:

- Rust: cargo build && cargo test; cargo tarpaulin -o Xml
- C/C++: cmake .. && cmake --build . && ctest; gcovr or llvm-cov for coverage
- Python: pip install -e .[test]; pytest -q --cov=python/pkg --cov-report=xml

## Ideal State

- Critical metrics must pass
- Incremental coverage for changes enforced
- Total coverage tracked, not blocking
- Focus on incremental quality improvement

## Exceptions and Overrides

When overrides are acceptable:

- Generated code (protobuf/bindings)
- Deprecated code being removed soon
- Experimental features behind feature flags/macros/extras
- Emergency hotfixes (must add tests in follow-up)

Override process example:

```bash
git commit -m "Fix: Critical production issue

OVERRIDE: Coverage — emergency fix, tests in follow-up PR #123"
```

## Metrics Tracking

Weekly metrics review (trends vs absolutes):

- Is composite coverage increasing week-over-week?
- Are build times stable and acceptable?
- Is total test time reasonable?
- Are we improving incrementally?

Monthly goals (example):

- Month 1: 20% total coverage
- Month 2: 25% total coverage
- Month 3: 30% total coverage
- ...
- Month N: 80% total coverage

## Tool Configuration (Examples)

Rust — Cargo Tarpaulin:

```toml
# tarpaulin.toml
[default]
exclude-files = [
    "*/build.rs",
    "*/tests/*",
    "*/target/*",
    "*/proto/*"  # Generated code
]
timeout = "120s"
skip-clean = false
```

C/C++ — gcovr (or llvm-cov):

```bash
# Example gcovr invocation (GCC/gcov)
ctest --output-on-failure
gcovr -r . --xml -o coverage.xml --exclude tests --exclude external

# Example llvm-cov (Clang/LLVM)
llvm-profdata merge -sparse default.profraw -o default.profdata
llvm-cov export ./build/your_binary -instr-profile=default.profdata \
  -format=lcov > coverage.info
```

Python — pytest-cov (pyproject.toml excerpt):

```toml
[tool.pytest.ini_options]
addopts = "-q --cov=python/pkg --cov-report=xml"
```

Static Analysis (non-blocking by default):

- Rust: cargo clippy -- -D warnings (optionally non-blocking at first)
- C/C++: cppcheck or clang-tidy (report only; consider -Werror phase-in)
- Python: ruff check ., mypy . (report only; tighten over time)
- Security: cargo audit, pip-audit (report only; escalate for criticals)

## CI Pipeline (Conceptual Mapping)

Critical (must pass):

- Rust: cargo build; cargo test
- C/C++: cmake configure/build; ctest
- Python: install; pytest
- Cross-Layer: FFI/integration tests (Rust ↔ C/C++; Python ↔ Native/Rust)
- Stub detection across languages (public surfaces)

Informational (report only):

- Coverage: tarpaulin/grcov (Rust), gcovr/llvm-cov (C/C++), pytest-cov (Python)
- Static analysis: clippy/cppcheck/clang-tidy/ruff/mypy
- Security/deps: cargo audit, pip-audit
- Packaging: build Python wheel/sdist; ensure shared libs and docs build

## Summary

Perfect is the enemy of good; incremental improvement is the path to excellence.

- Incremental approach is the decided strategy for committing code
- Ship working code with reasonable tests
- Improve coverage incrementally
- Enforce critical quality gates
- Track aspirational metrics without blocking
- Ensure repository quality only increases over time

This approach improves code quality continuously without blocking development, while maintaining strict standards for critical functionality across Rust, C/C++, and Python layers.
