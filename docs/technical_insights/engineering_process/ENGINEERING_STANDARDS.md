# Engineering Standards

This document defines the engineering standards and best practices for the ADA project.

## Development Philosophy

### Core Value vs Engineering Efficiency

**BUILD (Core Innovation)**:
Focus engineering effort on what makes ADA unique:
- Tracer dual-lane architecture
- Native agent with Frida hooks
- Token-budget-aware query engine
- MCP protocol implementation
- Custom ATF format

**BUY/REUSE (Engineering Efficiency)**:
Use existing mature tools for everything else:
- Coverage tools (lcov, diff-cover, genhtml)
- Testing frameworks (Google Test, pytest)
- Linting/formatting (clippy, black, clang-format)
- CI/CD (GitHub Actions)
- Documentation (mdBook, Sphinx)

**Decision Rule**: If it's not part of the tracing/analysis pipeline, use an existing solution.

## Code Quality Standards

### Mandatory Requirements

1. **Build Success**: 100% - ALL components must build
2. **Test Success**: 100% - ALL tests must pass  
3. **Test Coverage**: 100% - NO gaps in changed code
4. **Integration Score**: 100/100 - NO exceptions

### No Bypass Mechanisms

- **NO ignoring tests** - Fix root cause instead
- **NO reducing requirements** - 100% is mandatory
- **NO temporary compromises** - Quality is permanent
- **NO git commit --no-verify** - Hooks are mandatory

## Testing Standards

### Test Naming Conventions

All tests must be self-descriptive using behavioral naming:

**Pattern A**: `<unit>__<condition>__then_<expected>`
**Pattern B**: `should_<expected>_when_<condition>_for_<unit>`

Examples:
- ✅ `cache__evict_when_full__then_remove_lru_entry`
- ✅ `should_emit_event_when_threshold_exceeded_for_counter`
- ❌ `test_cache_basic`
- ❌ `test1`

### Test Output

Each test should print a descriptive header:
```c
printf("[CACHE] evict_when_full → removes LRU entry\n");
```

### Testing Frameworks

| Language | Framework | Runner |
|----------|-----------|--------|
| Rust | Built-in | `cargo test` |
| C/C++ | Google Test | Via CMake + Cargo |
| Python | pytest | `pytest` |

## Code Style Guidelines

### General Rules

1. **No comments unless requested** - Code should be self-documenting
2. **Follow existing patterns** - Consistency over personal preference
3. **Use existing utilities** - Don't reinvent in-codebase tools

### Language-Specific Style

#### Rust
- Format: `rustfmt` with default settings
- Linting: `clippy` with pedantic warnings
- Naming: `snake_case` for functions, `PascalCase` for types

#### C/C++
- Format: `clang-format` with project `.clang-format`
- Standard: C++17 minimum
- Headers: Use `#pragma once`

#### Python
- Format: `black` with default settings
- Linting: `ruff` for fast comprehensive checks
- Type hints: Required for public APIs

## Version Control

### Commit Messages

Follow conventional commits format:
```
<type>(<scope>): <subject>

<body>

<footer>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

### Branch Strategy

- `main` - Stable, production-ready code
- `feature/*` - New features
- `fix/*` - Bug fixes
- `docs/*` - Documentation only

### Pull Request Requirements

1. All CI checks must pass
2. 100% coverage on changed lines
3. At least one review approval
4. Commits squashed before merge

## Documentation Standards

### Code Documentation

#### Rust
```rust
/// Brief description.
///
/// Detailed explanation if needed.
///
/// # Arguments
/// * `param` - Description
///
/// # Returns
/// Description of return value
///
/// # Errors
/// When this returns `Err`
```

#### C/C++
```cpp
/**
 * @brief Brief description
 * 
 * Detailed explanation if needed.
 * 
 * @param param Description
 * @return Description of return value
 * @throws ExceptionType When thrown
 */
```

#### Python
```python
def function(param: str) -> int:
    """Brief description.
    
    Detailed explanation if needed.
    
    Args:
        param: Description
        
    Returns:
        Description of return value
        
    Raises:
        ValueError: When invalid input
    """
```

### Documentation Files

- **README.md**: Project overview and quick start only
- **GETTING_STARTED.md**: Detailed setup instructions
- **ARCHITECTURE.md**: System design and components
- **Component docs**: In `{component}/docs/design/`

## Performance Standards

### Benchmarking

- Use `criterion` for Rust microbenchmarks
- Use `Google Benchmark` for C++
- Use `pytest-benchmark` for Python

### Performance Targets

- Index lane: <1% CPU overhead
- Event throughput: 5M+ events/sec
- Memory usage: <100MB per process
- Startup time: <100ms

### Profiling Tools

- CPU: `perf` (Linux), Instruments (macOS)
- Memory: Valgrind, AddressSanitizer
- Tracing: Our own tool (dogfooding)

## Security Standards

### Never Commit

- Passwords, API keys, tokens
- Private keys or certificates  
- Internal URLs or endpoints
- Customer data

### Security Practices

1. Use environment variables for secrets
2. Validate all external input
3. Use safe string operations
4. Enable compiler security flags
5. Run security linters

### Platform Security

- macOS: Respect codesigning requirements
- Linux: Follow principle of least privilege
- All: Use secure random number generation

## Build System Standards

### Cargo as Orchestrator

ALL builds must go through Cargo:
```bash
cargo build --release  # Never run cmake directly
cargo test            # Never run ctest directly
```

### Coverage Workflow

```bash
# Use existing tools, not custom implementations
./utils/run_coverage.sh  # Simple wrapper around:
# - cargo-llvm-cov for Rust
# - lcov for merging
# - genhtml for reports
# - diff-cover for enforcement
```

### Binary Artifacts

- Never commit compiled binaries
- Use `.gitignore` properly
- Build artifacts go in `target/`

## Tool Requirements

### Mandatory Tools

Install via standard package managers:
```bash
# macOS
brew install llvm lcov cmake

# Rust
cargo install cargo-llvm-cov clippy rustfmt

# Python  
pip install black ruff pytest pytest-cov diff-cover coverage-lcov
```

### IDE Configuration

- Use `.editorconfig` for basic formatting
- Point to `target/compile_commands.json` for C++
- Use workspace settings in `.vscode/`

## Review Checklist

Before committing:
- [ ] Code builds without warnings
- [ ] All tests pass
- [ ] 100% coverage on changed lines
- [ ] No hardcoded values
- [ ] No commented-out code
- [ ] Follows naming conventions
- [ ] No binary files

Before PR:
- [ ] Commits are logical units
- [ ] PR description explains "why"
- [ ] Documentation updated if needed
- [ ] Performance impact considered
- [ ] Security implications reviewed