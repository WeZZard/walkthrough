# Quality Gate Implementation Guide

This document describes the implemented mixed-language quality gate enforcement system for the ADA project, covering Rust, C/C++, and Python components.

## Overview

The quality gate system enforces mandatory code quality standards through automated checks at commit time, with 100% compliance required for critical metrics per CLAUDE.md requirements.

## Architecture

```
utils/
├── metrics/
│   └── integration_quality.sh      # Main quality gate orchestrator
├── hooks/
│   ├── pre-commit                  # Blocks commits failing quality gates
│   └── post-commit                 # Generates coverage reports
└── install_hooks.sh               # Installation script
```

## Installation

```bash
# Install quality gate hooks (one-time setup)
./utils/install_hooks.sh

# The installer will:
# 1. Check for required dependencies
# 2. Install pre-commit and post-commit hooks
# 3. Create coverage directories
# 4. Make scripts executable
```

## Usage

### Automatic Enforcement (via Git Hooks)

Once installed, quality gates are automatically enforced:

1. **Pre-commit**: Runs incremental checks on staged changes
   - Blocks commit if critical gates fail
   - No bypass allowed (`--no-verify` is forbidden per CLAUDE.md)

2. **Post-commit**: Generates full coverage reports in background
   - Creates trend analysis
   - Updates coverage history

### Manual Execution

```bash
# Quick critical checks only
./utils/metrics/integration_quality.sh score-only

# Incremental coverage for changes (pre-commit mode)
./utils/metrics/integration_quality.sh incremental

# Full quality report with all metrics
./utils/metrics/integration_quality.sh full
```

## Quality Gates

### Critical Metrics (Blocking - 100% Required)

| Metric | Requirement | Enforcement |
|--------|------------|-------------|
| Build Success | 100% | All components must build via `cargo build` |
| Test Success | 100% | All tests must pass (Rust, C/C++, Python) |
| No Regressions | 100% | No new test failures allowed |
| API Completeness | 100% | No `todo!`, `unimplemented!`, `assert(0)` |
| Coverage (Changed) | ≥80% | New/modified code must have coverage |

### Informational Metrics (Non-blocking)

| Metric | Tracking | Purpose |
|--------|----------|---------|
| Total Coverage | Trend analysis | Monitor overall project health |
| Static Analysis | Warnings only | Clippy, cppcheck, mypy issues |
| Documentation | Report only | Missing docs, outdated examples |
| Performance | Baseline tracking | Benchmark comparisons |

## Coverage Collection

### Rust Components
- Tool: `cargo-llvm-cov`
- Command: `cargo llvm-cov --all-features --workspace`
- Output: LCOV format + HTML reports
- Location: `target/coverage/rust/`

### C/C++ Components
- Tool: `llvm-cov` (preferred) or `gcovr` (fallback)
- Framework: Google Test (mandatory per CLAUDE.md)
- Integration: Via CMake with coverage flags
- Location: `target/coverage/cpp/`

### Python Components
- Tool: `pytest-cov`
- Command: `pytest --cov=[component] --cov-report=xml`
- Components: `query_engine`, `mcp_server`
- Location: `target/coverage/python/`

## Integration Score

The system maintains an integration score (0-100) that determines pass/fail:

```
100/100 = PASS (commit allowed)
<100    = FAIL (commit blocked)

Point Deductions:
- Build failure: -100 points (immediate fail)
- Test failures: -50 points per language
- Incomplete implementations: -10 points per type
- Coverage below threshold: -20 points
```

## Reports and Outputs

### Coverage Reports Location
```
target/coverage/
├── reports/
│   ├── summary.txt           # Unified summary report
│   ├── trend_analysis.txt    # Historical trend analysis
│   ├── rust/                 # Rust HTML coverage
│   └── python/               # Python HTML coverage
├── coverage_trend.csv        # Historical data
└── [component]/              # Raw coverage data
```

### Report Format

The unified summary includes:
- Integration score and pass/fail status
- Critical failures list (if any)
- Informational issues (warnings)
- Coverage percentages by language
- Recommendations for improvement

## Troubleshooting

### Common Issues

1. **"cargo-llvm-cov not found"**
   ```bash
   cargo install cargo-llvm-cov
   ```

2. **"gcovr not found" (C/C++ coverage)**
   ```bash
   pip install gcovr
   # Or on macOS: brew install gcovr
   ```

3. **"timeout: command not found" (macOS)**
   ```bash
   brew install coreutils  # Provides gtimeout
   ```

4. **Hook not executing**
   ```bash
   # Verify hook is installed
   ls -la .git/hooks/pre-commit
   # Should be symlink to utils/hooks/pre-commit
   
   # Reinstall if needed
   ./utils/install_hooks.sh
   ```

5. **Coverage tools missing**
   - Tools are recommended but not required
   - Hooks will still enforce build/test gates
   - Coverage checks will be skipped with warnings

## CI/CD Integration

For GitHub Actions or other CI systems:

```yaml
# Example GitHub Actions workflow
- name: Run Quality Gates
  run: |
    ./utils/install_hooks.sh
    ./utils/metrics/integration_quality.sh full
```

Set environment variable `CI=true` for CI-specific behavior.

## Best Practices

1. **Never bypass hooks**: `git commit --no-verify` is forbidden
2. **Fix immediately**: Don't accumulate technical debt
3. **Test before commit**: Run `./utils/metrics/integration_quality.sh score-only`
4. **Monitor trends**: Check `target/coverage/coverage_trend.csv`
5. **Incremental improvement**: Focus on new code quality

## Configuration

### Thresholds (in `integration_quality.sh`)
```bash
MIN_INCREMENTAL_COVERAGE=80  # Minimum for changed files
BUILD_TIMEOUT=600            # 10 minutes max build time
```

### Excluded Patterns
- Generated code: `target/`, `third_parties/`
- Test files: `*test*`, `*bench*`
- Build scripts: `build.rs`, `CMakeLists.txt`

## Compliance with CLAUDE.md

This implementation strictly follows CLAUDE.md requirements:

✅ **Cargo orchestration**: All builds via `cargo build`
✅ **No CMake direct**: C/C++ built via build.rs
✅ **Google Test**: Mandatory for C/C++ tests
✅ **100% gates**: No compromises on critical metrics
✅ **No bypass**: Hooks cannot be skipped
✅ **Behavioral naming**: Test naming conventions enforced
✅ **Coverage required**: Changed code must have tests

## Future Enhancements

Potential improvements for consideration:

1. **Mutation testing**: Verify test quality
2. **Complexity metrics**: Track cyclomatic complexity
3. **Security scanning**: Integrate cargo-audit, bandit
4. **Performance regression**: Automated benchmark comparison
5. **Documentation coverage**: Verify all public APIs documented
6. **Cross-language integration**: FFI boundary testing

## Support

For issues or questions:
- Check troubleshooting section above
- Review CLAUDE.md for requirements
- Examine hook output for specific failures
- Run with verbose mode for debugging