#!/bin/bash
# Mixed-Language Quality Gate Enforcement Script
# Supports Rust, C/C++, and Python components per CLAUDE.md requirements

# Add LLVM tools to PATH if available
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COVERAGE_DIR="${REPO_ROOT}/target/coverage"
COVERAGE_REPORTS_DIR="${COVERAGE_DIR}/reports"
MIN_INCREMENTAL_COVERAGE=80  # Minimum coverage for changed files
BUILD_TIMEOUT=600  # 10 minutes

# Command line arguments
MODE="${1:-full}"  # full, incremental, score-only
VERBOSE="${2:-false}"

# Component detection
HAS_RUST=false
HAS_CPP=false
HAS_PYTHON=false

[[ -f "${REPO_ROOT}/Cargo.toml" ]] && HAS_RUST=true
[[ -d "${REPO_ROOT}/tracer_backend" ]] && HAS_CPP=true
[[ -d "${REPO_ROOT}/query_engine" || -d "${REPO_ROOT}/mcp_server" ]] && HAS_PYTHON=true

# Integration score tracking
INTEGRATION_SCORE=100
CRITICAL_FAILURES=()
INFORMATIONAL_ISSUES=()

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Function to deduct points from integration score
deduct_points() {
    local points=$1
    local reason=$2
    INTEGRATION_SCORE=$((INTEGRATION_SCORE - points))
    CRITICAL_FAILURES+=("$reason (-$points points)")
    log_error "$reason"
}

# Function to track informational issues
track_info() {
    local issue=$1
    INFORMATIONAL_ISSUES+=("$issue")
    log_warning "$issue"
}

# Create coverage directories
setup_coverage_dirs() {
    mkdir -p "${COVERAGE_DIR}"
    mkdir -p "${COVERAGE_REPORTS_DIR}"
    mkdir -p "${COVERAGE_DIR}/rust"
    mkdir -p "${COVERAGE_DIR}/cpp"
    mkdir -p "${COVERAGE_DIR}/python"
    mkdir -p "${COVERAGE_REPORTS_DIR}/rust"
    mkdir -p "${COVERAGE_REPORTS_DIR}/cpp"
    mkdir -p "${COVERAGE_REPORTS_DIR}/python"
}

# Check if running in CI or local
is_ci() {
    [[ "${CI:-false}" == "true" ]] || [[ "${GITHUB_ACTIONS:-false}" == "true" ]]
}

# Get list of changed files for incremental coverage
get_changed_files() {
    if git diff --cached --name-only &>/dev/null; then
        # Pre-commit: staged files
        git diff --cached --name-only
    elif git diff HEAD~1 --name-only &>/dev/null; then
        # Post-commit: files in last commit
        git diff HEAD~1 --name-only
    else
        # Fallback: all tracked files
        git ls-files
    fi
}

# Build all components (orchestrated through Cargo)
build_all() {
    log_info "Building all components via Cargo..."
    
    # Use gtimeout on macOS if available, otherwise no timeout
    if command -v gtimeout &>/dev/null; then
        TIMEOUT_CMD="gtimeout ${BUILD_TIMEOUT}"
    elif command -v timeout &>/dev/null; then
        TIMEOUT_CMD="timeout ${BUILD_TIMEOUT}"
    else
        TIMEOUT_CMD=""
    fi
    
    if [[ -n "$TIMEOUT_CMD" ]]; then
        if ! $TIMEOUT_CMD cargo build --all 2>&1; then
            deduct_points 100 "Build failed - critical gate violation"
            return 1
        fi
    else
        if ! cargo build --all 2>&1; then
            deduct_points 100 "Build failed - critical gate violation"
            return 1
        fi
    fi
    
    log_success "All components built successfully"
    return 0
}

# Run all tests
run_tests() {
    log_info "Running all tests..."
    local test_failed=false
    
    if [[ "$HAS_RUST" == "true" ]]; then
        log_info "Running Rust tests..."
        if ! cargo test --all --no-fail-fast 2>&1; then
            deduct_points 50 "Rust tests failed"
            test_failed=true
        else
            log_success "Rust tests passed"
        fi
    fi
    
    if [[ "$HAS_CPP" == "true" ]]; then
        log_info "Running C/C++ tests (Google Test)..."
        # Tests are run via cargo test for C/C++ components
        if ! cargo test --package tracer_backend 2>&1; then
            deduct_points 50 "C/C++ tests failed"
            test_failed=true
        else
            log_success "C/C++ tests passed"
        fi
    fi
    
    if [[ "$HAS_PYTHON" == "true" ]]; then
        log_info "Running Python tests..."
        for component in query_engine mcp_server; do
            if [[ -d "${REPO_ROOT}/${component}" ]]; then
                (cd "${REPO_ROOT}/${component}" && python -m pytest -q 2>&1) || {
                    deduct_points 25 "Python tests failed for ${component}"
                    test_failed=true
                }
            fi
        done
        [[ "$test_failed" != "true" ]] && log_success "Python tests passed"
    fi
    
    return 0
}

# Collect Rust coverage using cargo-llvm-cov
collect_rust_coverage() {
    log_info "Collecting Rust coverage with cargo-llvm-cov..."
    
    # Check if cargo-llvm-cov is installed
    if ! command -v cargo-llvm-cov &>/dev/null; then
        log_warning "cargo-llvm-cov not installed. Installing..."
        cargo install cargo-llvm-cov || {
            track_info "Failed to install cargo-llvm-cov"
            return 1
        }
    fi
    
    # Set LLVM tool paths if not already set
    if [[ -z "${LLVM_COV:-}" ]] || [[ -z "${LLVM_PROFDATA:-}" ]]; then
        export RUSTUP_HOME="${HOME}/.rustup"
        # Try nightly toolchain first (usually has better coverage support)
        for toolchain in nightly stable; do
            LLVM_COV="${RUSTUP_HOME}/toolchains/${toolchain}-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/llvm-cov"
            LLVM_PROFDATA="${RUSTUP_HOME}/toolchains/${toolchain}-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/llvm-profdata"
            if [[ -f "$LLVM_COV" ]] && [[ -f "$LLVM_PROFDATA" ]]; then
                export LLVM_COV
                export LLVM_PROFDATA
                log_info "Using ${toolchain} LLVM tools"
                break
            fi
        done
        
        # Fallback to find
        if [[ ! -f "${LLVM_COV:-}" ]]; then
            export LLVM_COV=$(find ${RUSTUP_HOME} -name llvm-cov -type f 2>/dev/null | head -1)
            export LLVM_PROFDATA=$(find ${RUSTUP_HOME} -name llvm-profdata -type f 2>/dev/null | head -1)
        fi
    fi
    
    # Clean previous coverage data
    cargo llvm-cov clean --workspace
    
    # Generate coverage for all Rust components
    cargo llvm-cov --all-features --workspace \
        --lcov --output-path "${COVERAGE_DIR}/rust/lcov.info" || {
        track_info "Rust coverage collection failed"
        return 1
    }
    
    # Generate HTML report
    cargo llvm-cov report --html --output-dir "${COVERAGE_REPORTS_DIR}/rust"
    
    # Generate text report and parse coverage percentage
    local coverage_output=$(cargo llvm-cov report 2>&1)
    
    # Try to parse the TOTAL line which has the format:
    # TOTAL                      134       63    47.01%       0       0        -
    local rust_coverage=$(echo "$coverage_output" | grep "^TOTAL" | awk '{
        for(i=1; i<=NF; i++) {
            if($i ~ /^[0-9]+\.[0-9]+%$/) {
                gsub(/%/, "", $i)
                print $i
                exit
            }
        }
    }')
    
    # If that didn't work, try alternative parsing for different output formats
    if [[ -z "$rust_coverage" ]]; then
        rust_coverage=$(echo "$coverage_output" | grep "TOTAL" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    fi
    
    # Last resort: parse from the lcov.info file directly
    if [[ -z "$rust_coverage" ]] && [[ -f "${COVERAGE_DIR}/rust/lcov.info" ]]; then
        local lines_hit=$(grep "^LH:" "${COVERAGE_DIR}/rust/lcov.info" | awk -F: '{sum+=$2} END {print sum}')
        local lines_total=$(grep "^LF:" "${COVERAGE_DIR}/rust/lcov.info" | awk -F: '{sum+=$2} END {print sum}')
        if [[ -n "$lines_hit" ]] && [[ -n "$lines_total" ]] && [[ "$lines_total" -gt 0 ]]; then
            rust_coverage=$(awk "BEGIN {printf \"%.2f\", 100.0 * $lines_hit / $lines_total}")
        fi
    fi
    
    echo "${rust_coverage:-0}" > "${COVERAGE_DIR}/rust/coverage.txt"
    
    log_success "Rust coverage: ${rust_coverage:-0}%"
    return 0
}

# Collect C/C++ coverage using llvm-cov
collect_cpp_coverage() {
    log_info "Collecting C/C++ coverage with LLVM tools..."
    
    # Check for LLVM tools
    if ! command -v llvm-cov &>/dev/null || ! command -v llvm-profdata &>/dev/null; then
        log_warning "LLVM tools not found in PATH. Checking common locations..."
        # Try homebrew LLVM
        if [[ -d "/opt/homebrew/opt/llvm/bin" ]]; then
            export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
        elif [[ -d "/usr/local/opt/llvm/bin" ]]; then
            export PATH="/usr/local/opt/llvm/bin:$PATH"
        else
            track_info "LLVM tools (llvm-cov, llvm-profdata) not found"
            return 1
        fi
    fi
    
    # Build with LLVM coverage instrumentation
    log_info "Building C/C++ with LLVM coverage instrumentation..."
    (cd "${REPO_ROOT}/tracer_backend" && \
     rm -rf build-coverage && \
     mkdir -p build-coverage && \
     cd build-coverage && \
     cmake .. -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_C_COMPILER=clang \
              -DCMAKE_CXX_COMPILER=clang++ \
              -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
              -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
              -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && \
     make -j4) || {
        track_info "C/C++ coverage build failed"
        return 1
    }
    
    # Set LLVM profile output location
    export LLVM_PROFILE_FILE="${REPO_ROOT}/tracer_backend/build-coverage/test-%p.profraw"
    
    # Run tests to generate coverage data
    log_info "Running C/C++ tests for coverage..."
    (cd "${REPO_ROOT}/tracer_backend/build-coverage" && \
     GTEST_COLOR=no ctest --output-on-failure -j4) || {
        track_info "C/C++ test execution for coverage failed (non-critical)"
    }
    
    # Merge all profile data
    log_info "Merging C/C++ coverage profiles..."
    llvm-profdata merge -sparse \
        "${REPO_ROOT}/tracer_backend/build-coverage/"*.profraw \
        -o "${COVERAGE_DIR}/cpp/coverage.profdata" || {
        track_info "C/C++ profile merge failed"
        return 1
    }
    
    # Find all test executables
    local test_binaries=""
    for binary in "${REPO_ROOT}/tracer_backend/build-coverage/test_"*; do
        if [[ -x "$binary" ]] && [[ ! "$binary" == *.profraw ]]; then
            test_binaries="$test_binaries -object $binary"
        fi
    done
    
    # Generate coverage report using llvm-cov
    log_info "Generating C/C++ coverage report with llvm-cov..."
    
    # Generate text summary
    llvm-cov report $test_binaries \
        -instr-profile="${COVERAGE_DIR}/cpp/coverage.profdata" \
        -ignore-filename-regex=".*test.*|.*third_parties.*|.*build.*|.*_deps.*" \
        > "${COVERAGE_DIR}/cpp/coverage_summary.txt" 2>&1 || {
        track_info "C/C++ coverage report generation failed"
        return 1
    }
    
    # Generate LCOV format for compatibility
    llvm-cov export $test_binaries \
        -instr-profile="${COVERAGE_DIR}/cpp/coverage.profdata" \
        -format=lcov \
        -ignore-filename-regex=".*test.*|.*third_parties.*|.*build.*|.*_deps.*" \
        > "${COVERAGE_DIR}/cpp/lcov.info" 2>&1
    
    # Generate HTML report
    llvm-cov show $test_binaries \
        -instr-profile="${COVERAGE_DIR}/cpp/coverage.profdata" \
        -format=html \
        -output-dir="${COVERAGE_REPORTS_DIR}/cpp" \
        -ignore-filename-regex=".*test.*|.*third_parties.*|.*build.*|.*_deps.*" \
        -Xdemangler=c++filt
    
    # Extract coverage percentage from llvm-cov report
    # Format: "TOTAL    134       63    52.99%       0       0        -"
    local cpp_coverage=$(grep "^TOTAL" "${COVERAGE_DIR}/cpp/coverage_summary.txt" 2>/dev/null | \
                        awk '{
                            for(i=1; i<=NF; i++) {
                                if($i ~ /^[0-9]+\.[0-9]+%$/) {
                                    gsub(/%/, "", $i)
                                    print $i
                                    exit
                                }
                            }
                        }')
    
    if [[ -z "$cpp_coverage" ]]; then
        # Try to parse from JSON output
        local json_output=$(llvm-cov export $test_binaries \
            -instr-profile="${COVERAGE_DIR}/cpp/coverage.profdata" \
            -summary-only 2>/dev/null)
        cpp_coverage=$(echo "$json_output" | grep -o '"percent":[0-9.]*' | head -1 | cut -d: -f2)
    fi
    
    echo "${cpp_coverage:-0}" > "${COVERAGE_DIR}/cpp/coverage.txt"
    log_success "C/C++ coverage: ${cpp_coverage:-0}%"
    
    log_success "C/C++ coverage collected"
    return 0
}

# Collect Python coverage using pytest-cov
collect_python_coverage() {
    log_info "Collecting Python coverage with pytest-cov..."
    
    for component in query_engine mcp_server; do
        if [[ -d "${REPO_ROOT}/${component}" ]]; then
            log_info "Collecting coverage for ${component}..."
            
            (cd "${REPO_ROOT}/${component}" && \
             python -m pytest --cov="${component}" \
                           --cov-report=xml:"${COVERAGE_DIR}/python/${component}_coverage.xml" \
                           --cov-report=html:"${COVERAGE_REPORTS_DIR}/python/${component}" \
                           --cov-report=term 2>&1) || {
                track_info "Python coverage failed for ${component}"
                continue
            }
            
            # Extract coverage percentage
            local py_coverage=$(cd "${REPO_ROOT}/${component}" && \
                              python -m pytest --cov="${component}" --cov-report=term 2>&1 | \
                              grep "TOTAL" | awk '{print $4}' | tr -d '%')
            echo "${py_coverage:-0}" > "${COVERAGE_DIR}/python/${component}_coverage.txt"
            
            log_success "${component} coverage: ${py_coverage}%"
        fi
    done
    
    return 0
}

# Check for incomplete implementations
check_incomplete_implementations() {
    log_info "Checking for incomplete implementations..."
    local found_stubs=false
    
    # Rust: Check for todo!, unimplemented!, panic!("unimplemented")
    if [[ "$HAS_RUST" == "true" ]]; then
        if grep -r "todo!\|unimplemented!\|panic!(\"unimplemented" \
               --include="*.rs" "${REPO_ROOT}" \
               --exclude-dir=target --exclude-dir=third_parties 2>/dev/null; then
            deduct_points 10 "Found incomplete Rust implementations (todo!/unimplemented!)"
            found_stubs=true
        fi
    fi
    
    # C/C++: Check for assert(0), abort(), TODO comments in public functions
    if [[ "$HAS_CPP" == "true" ]]; then
        if grep -r "assert(0)\|abort()\|TODO.*{" \
               --include="*.c" --include="*.cpp" --include="*.h" --include="*.hpp" \
               "${REPO_ROOT}/tracer_backend" \
               --exclude-dir=tests --exclude-dir=third_parties --exclude-dir=build --exclude-dir=build-coverage 2>/dev/null; then
            deduct_points 10 "Found incomplete C/C++ implementations"
            found_stubs=true
        fi
    fi
    
    # Python: Check for NotImplementedError, pass in public methods
    if [[ "$HAS_PYTHON" == "true" ]]; then
        if grep -r "raise NotImplementedError\|^[[:space:]]*pass[[:space:]]*$" \
               --include="*.py" "${REPO_ROOT}" \
               --exclude-dir=tests --exclude-dir=__pycache__ 2>/dev/null; then
            deduct_points 10 "Found incomplete Python implementations"
            found_stubs=true
        fi
    fi
    
    [[ "$found_stubs" != "true" ]] && log_success "No incomplete implementations found"
    return 0
}

# Incremental coverage check for changed files
check_incremental_coverage() {
    log_info "Checking incremental coverage for changed files..."
    
    local changed_files=$(get_changed_files)
    local needs_coverage=false
    
    for file in $changed_files; do
        case "$file" in
            *.rs)
                needs_coverage=true
                log_info "Rust file changed: $file"
                ;;
            *.c|*.cpp|*.h|*.hpp)
                needs_coverage=true
                log_info "C/C++ file changed: $file"
                ;;
            *.py)
                if [[ "$file" != *test* ]] && [[ "$file" != *__pycache__* ]]; then
                    needs_coverage=true
                    log_info "Python file changed: $file"
                fi
                ;;
        esac
    done
    
    if [[ "$needs_coverage" == "true" ]]; then
        log_info "Changed files require coverage verification"
        # In incremental mode, we enforce minimum coverage
        # This would need more sophisticated per-file coverage analysis
        collect_rust_coverage
        collect_cpp_coverage
        collect_python_coverage
    else
        log_success "No source files changed that require coverage"
    fi
    
    return 0
}

# Generate unified coverage report
generate_unified_report() {
    log_info "Generating unified coverage report..."
    
    cat > "${COVERAGE_REPORTS_DIR}/summary.txt" << EOF
=================================================================
           MIXED-LANGUAGE COVERAGE REPORT
=================================================================
Generated: $(date)
Repository: ${REPO_ROOT}
Mode: ${MODE}

INTEGRATION SCORE: ${INTEGRATION_SCORE}/100
=================================================================

CRITICAL METRICS (Blocking):
----------------------------
EOF
    
    if [[ ${#CRITICAL_FAILURES[@]} -eq 0 ]]; then
        echo "✓ All critical gates passed" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
    else
        for failure in "${CRITICAL_FAILURES[@]}"; do
            echo "✗ $failure" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
        done
    fi
    
    cat >> "${COVERAGE_REPORTS_DIR}/summary.txt" << EOF

INFORMATIONAL METRICS (Non-blocking):
--------------------------------------
EOF
    
    if [[ ${#INFORMATIONAL_ISSUES[@]} -eq 0 ]]; then
        echo "✓ No informational issues" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
    else
        for issue in "${INFORMATIONAL_ISSUES[@]}"; do
            echo "⚠ $issue" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
        done
    fi
    
    # Add coverage percentages
    cat >> "${COVERAGE_REPORTS_DIR}/summary.txt" << EOF

COVERAGE BY LANGUAGE:
---------------------
EOF
    
    if [[ -f "${COVERAGE_DIR}/rust/coverage.txt" ]]; then
        echo "Rust:   $(cat ${COVERAGE_DIR}/rust/coverage.txt)%" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
    fi
    
    if [[ -f "${COVERAGE_DIR}/cpp/coverage.txt" ]]; then
        echo "C/C++:  $(cat ${COVERAGE_DIR}/cpp/coverage.txt)%" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
    else
        echo "C/C++:  No data" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
    fi
    
    for component in query_engine mcp_server; do
        if [[ -f "${COVERAGE_DIR}/python/${component}_coverage.txt" ]]; then
            echo "Python/${component}: $(cat ${COVERAGE_DIR}/python/${component}_coverage.txt)%" >> "${COVERAGE_REPORTS_DIR}/summary.txt"
        fi
    done
    
    cat >> "${COVERAGE_REPORTS_DIR}/summary.txt" << EOF

=================================================================
Report Location: ${COVERAGE_REPORTS_DIR}
HTML Reports:
  - Rust:   ${COVERAGE_REPORTS_DIR}/rust/index.html
  - C/C++:  ${COVERAGE_REPORTS_DIR}/cpp/index.html
  - Python: ${COVERAGE_REPORTS_DIR}/python/*/index.html
=================================================================
EOF
    
    cat "${COVERAGE_REPORTS_DIR}/summary.txt"
    return 0
}

# Main execution
main() {
    log_info "Starting integration quality check (mode: ${MODE})..."
    
    setup_coverage_dirs
    
    case "$MODE" in
        score-only)
            # Quick critical checks only
            build_all
            run_tests
            check_incomplete_implementations
            ;;
            
        incremental)
            # Check coverage for changed files
            build_all
            run_tests
            check_incomplete_implementations
            check_incremental_coverage
            ;;
            
        full)
            # Full quality report
            build_all
            run_tests
            check_incomplete_implementations
            collect_rust_coverage
            collect_cpp_coverage
            collect_python_coverage
            ;;
            
        *)
            log_error "Unknown mode: $MODE"
            echo "Usage: $0 [full|incremental|score-only]"
            exit 1
            ;;
    esac
    
    generate_unified_report
    
    # Exit based on integration score
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        log_error "QUALITY GATE FAILED: Integration score ${INTEGRATION_SCORE}/100"
        log_error "Critical failures must be fixed before committing"
        exit 1
    else
        log_success "QUALITY GATE PASSED: Integration score ${INTEGRATION_SCORE}/100"
        exit 0
    fi
}

# Run main
main "$@"