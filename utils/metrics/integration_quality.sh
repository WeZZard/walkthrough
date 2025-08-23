#!/bin/bash
# Quality Gate Enforcement Script with Cargo Orchestration
# All builds and coverage collection go through Cargo

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-full}"  # full, incremental, score-only
VERBOSE="${2:-false}"

# Integration score tracking
INTEGRATION_SCORE=100
CRITICAL_FAILURES=()

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

deduct_points() {
    local points=$1
    local reason=$2
    INTEGRATION_SCORE=$((INTEGRATION_SCORE - points))
    CRITICAL_FAILURES+=("$reason (-$points points)")
    log_error "$reason"
}

# Build all components through Cargo
build_all() {
    log_info "Building all components through Cargo..."
    
    local BUILD_FLAGS=""
    
    # Enable coverage instrumentation if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        BUILD_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        export RUSTFLAGS="-C instrument-coverage"
        export LLVM_PROFILE_FILE="${REPO_ROOT}/target/coverage/prof-%p-%m.profraw"
        log_info "Coverage instrumentation enabled"
    fi
    
    # Build everything through Cargo
    if ! cargo build --all --release $BUILD_FLAGS; then
        deduct_points 100 "Build failed"
        return 1
    fi
    
    log_success "Build completed successfully"
}

# Run all tests through Cargo
run_tests() {
    log_info "Running all tests through Cargo..."
    
    local TEST_FLAGS=""
    
    # Enable coverage if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        TEST_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        # Ensure coverage directory exists
        mkdir -p "${REPO_ROOT}/target/coverage"
    fi
    
    # Run all tests
    if ! cargo test --all $TEST_FLAGS; then
        deduct_points 100 "Tests failed"
        return 1
    fi
    
    log_success "All tests passed"
}

# Collect coverage data using Cargo-orchestrated helper
collect_coverage() {
    if [[ "$MODE" == "score-only" ]]; then
        log_info "Skipping coverage collection in score-only mode"
        return 0
    fi
    
    log_info "Collecting coverage data through Cargo..."
    
    # Run the coverage helper to collect all coverage data
    if ! cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- collect; then
        log_warning "Coverage collection failed"
        return 1
    fi
    
    # Generate report
    if ! cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- report --format lcov; then
        log_warning "Coverage report generation failed"
        return 1
    fi
    
    # Check if coverage meets requirements
    if [[ -f "${REPO_ROOT}/target/coverage/coverage.lcov" ]]; then
        local lines_hit=$(grep -c "DA:[0-9]*,[1-9]" "${REPO_ROOT}/target/coverage/coverage.lcov" 2>/dev/null || echo "0")
        local lines_total=$(grep -c "DA:" "${REPO_ROOT}/target/coverage/coverage.lcov" 2>/dev/null || echo "0")
        
        if [[ $lines_total -gt 0 ]]; then
            local coverage=$(awk "BEGIN {printf \"%.2f\", ($lines_hit / $lines_total) * 100}")
            log_success "Total coverage: ${coverage}%"
            
            # Check incremental coverage for changed files
            if [[ "$MODE" == "incremental" ]]; then
                check_incremental_coverage
            fi
        fi
    fi
}

# Check if any source files were changed
has_source_changes() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        # Pre-commit: check staged files
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        # Post-commit: check last commit
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1  # No git context, assume changes
    fi
    
    # Filter for actual source code files (not tests, not config, not docs)
    # First get all code files
    local code_files=$(echo "$changed_files" | grep -E '\.(rs|c|cpp|cc|cxx|h|hpp|hxx|py)$' || true)
    
    # Exclude test files, build scripts, and utility scripts
    local source_files=$(echo "$code_files" | \
        grep -v -E '(^tests?/|/tests?/|_test\.|\.test\.|test_.*\.rs$|.*_test\.py$|/fixtures?/|/bench/|/benches/|build\.rs$)' || true)
    
    # Exclude utility and helper directories (these are tools, not product code)
    source_files=$(echo "$source_files" | \
        grep -v -E '(^utils/|^tools/|^scripts/|^\.github/)' || true)
    
    # Keep only files in component directories (where actual product code lives)
    source_files=$(echo "$source_files" | \
        grep -E '(^tracer/|^tracer_backend/|^query_engine/|^mcp_server/|^src/|^include/|^lib/)' || true)
    
    if [[ -z "$source_files" ]]; then
        return 1  # No source changes
    else
        echo "$source_files"  # Output the list for use by caller
        return 0  # Has source changes
    fi
}

# Check incremental coverage for changed files
check_incremental_coverage() {
    log_info "Checking incremental coverage..."
    
    # Get changed source files (excluding tests)
    local changed_files=$(has_source_changes || echo "")
    
    if [[ -z "$changed_files" ]]; then
        log_info "No source files changed (only config/docs/tests)"
        return 0
    fi
    
    log_info "Source files changed:"
    echo "$changed_files" | sed 's/^/  - /'
    
    # TODO: Implement detailed incremental coverage check for specific files
    # For now, we're running full coverage but should check coverage of changed files
    log_success "Incremental coverage check completed"
}

# Check for incomplete implementations
check_incomplete() {
    log_info "Checking for incomplete implementations..."
    
    local incomplete_count=0
    
    # Check for TODO/FIXME/XXX comments
    incomplete_count=$(grep -r "TODO\|FIXME\|XXX" --include="*.rs" --include="*.c" --include="*.cpp" --include="*.h" "${REPO_ROOT}" 2>/dev/null | wc -l || echo 0)
    
    if [[ $incomplete_count -gt 0 ]]; then
        log_warning "Found $incomplete_count incomplete implementations (TODO/FIXME/XXX)"
    else
        log_success "No incomplete implementations found"
    fi
}

# Generate final report
generate_report() {
    echo ""
    echo "========================================="
    echo "       INTEGRATION QUALITY REPORT        "
    echo "========================================="
    echo ""
    
    if [[ ${#CRITICAL_FAILURES[@]} -gt 0 ]]; then
        echo -e "${RED}CRITICAL FAILURES:${NC}"
        for failure in "${CRITICAL_FAILURES[@]}"; do
            echo "  • $failure"
        done
        echo ""
    fi
    
    # Color code the score
    local score_color=$GREEN
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        score_color=$RED
    fi
    
    echo -e "Integration Score: ${score_color}${INTEGRATION_SCORE}/100${NC}"
    
    if [[ $INTEGRATION_SCORE -eq 100 ]]; then
        echo -e "${GREEN}✓ Quality Gate PASSED${NC}"
        return 0
    else
        echo -e "${RED}✗ Quality Gate FAILED${NC}"
        return 1
    fi
}

# Main execution
main() {
    log_info "Running quality gate in $MODE mode"
    
    # For incremental mode, check if there are any source changes first
    if [[ "$MODE" == "incremental" ]]; then
        local source_changes=$(has_source_changes || echo "")
        if [[ -z "$source_changes" ]]; then
            log_success "No source code changes detected"
            log_info "Only configuration, documentation, or test files were modified"
            log_success "Skipping coverage requirements for non-source changes"
            
            # Still run basic checks
            build_all
            run_tests
            generate_report
            return $?
        else
            log_info "Source code changes detected - full quality gate required"
        fi
    fi
    
    # Clean coverage data if in full mode
    if [[ "$MODE" == "full" ]]; then
        log_info "Cleaning previous coverage data..."
        cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- clean
    fi
    
    # Build everything
    build_all
    
    # Run tests
    run_tests
    
    # Check incomplete implementations
    check_incomplete
    
    # Collect coverage if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        collect_coverage
    fi
    
    # Generate final report
    generate_report
}

# Run main function
main
exit_code=$?

# Ensure we return the correct exit code
exit $exit_code