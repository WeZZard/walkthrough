#!/bin/bash
# Unified Integration Quality Gate
# Human-readable quality reporting
# Zero dependencies except standard Unix tools

set -uo pipefail

# ============================================================================
# CONFIGURATION
# ============================================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-full}"  # full, incremental, score-only
VERBOSE="${VERBOSE:-false}"

# Paths
COVERAGE_DIR="${REPO_ROOT}/target/coverage"
COVERAGE_REPORT_DIR="${REPO_ROOT}/target/coverage_report"

# Colors for human output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Timing tracking - using simple variables for compatibility
# Format: SEGMENT_TIMES_NAME="duration_ms" SEGMENT_TIMES_NAME2="duration_ms"
SEGMENT_TIMES=""
SEGMENT_START_TIME=0

# Exit codes
CHECK_RESULT_PASSED=0
CHECK_RESULT_BLOCKED=1
CHECK_RESULT_DOCUMENT_ONLY=2

# Score tracking
INTEGRATION_SCORE=100
CRITICAL_FAILURES=()

# Temporary files for capturing output
BUILD_OUTPUT=$(mktemp)
TEST_OUTPUT=$(mktemp)
COVERAGE_OUTPUT=$(mktemp)
trap "rm -f $BUILD_OUTPUT $TEST_OUTPUT $COVERAGE_OUTPUT" EXIT

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

# Timing functions
start_timer() {
    local segment_name="$1"
    SEGMENT_START_TIME=$(date +%s%N)  # nanoseconds since epoch
    log_info "Starting: $segment_name"
}

end_timer() {
    local segment_name="$1"
    local end_time=$(date +%s%N)
    local duration_ns=$((end_time - SEGMENT_START_TIME))
    local duration_ms=$((duration_ns / 1000000))
    local duration_s=$((duration_ms / 1000))
    local duration_display=""

    if [[ $duration_s -gt 0 ]]; then
        duration_display="${duration_s}.$(printf "%03d" $((duration_ms % 1000)))s"
    else
        duration_display="${duration_ms}ms"
    fi

    # Append to timing list (name:duration_ms format)
    if [[ -z "$SEGMENT_TIMES" ]]; then
        SEGMENT_TIMES="${segment_name}:${duration_ms}"
    else
        SEGMENT_TIMES="${SEGMENT_TIMES}|${segment_name}:${duration_ms}"
    fi

    echo -e "${CYAN}[TIMING]${NC} $segment_name completed in $duration_display"
}

print_timing_summary() {
    echo -e "\n${CYAN}=== TIMING SUMMARY ===${NC}"
    local total_ms=0

    # Parse the timing string (format: name1:ms1|name2:ms2|...)
    IFS='|' read -ra TIMINGS <<< "$SEGMENT_TIMES"
    for timing in "${TIMINGS[@]}"; do
        if [[ "$timing" =~ ^([^:]+):([0-9]+)$ ]]; then
            local segment_name="${BASH_REMATCH[1]}"
            local duration_ms="${BASH_REMATCH[2]}"
            local duration_s=$((duration_ms / 1000))
            local duration_display=""

            if [[ $duration_s -gt 0 ]]; then
                duration_display="${duration_s}.$(printf "%03d" $((duration_ms % 1000)))s"
            else
                duration_display="${duration_ms}ms"
            fi

            printf "  %-40s %10s\n" "$segment_name:" "$duration_display"
            total_ms=$((total_ms + duration_ms))
        fi
    done

    local total_s=$((total_ms / 1000))
    local total_display=""
    if [[ $total_s -gt 60 ]]; then
        local minutes=$((total_s / 60))
        local seconds=$((total_s % 60))
        total_display="${minutes}m ${seconds}s"
    elif [[ $total_s -gt 0 ]]; then
        total_display="${total_s}.$(printf "%03d" $((total_ms % 1000)))s"
    else
        total_display="${total_ms}ms"
    fi

    echo -e "${CYAN}  ----------------------------------------${NC}"
    printf "  %-40s %10s\n" "TOTAL:" "$total_display"
    echo -e "${CYAN}======================${NC}\n"
}

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



# ============================================================================
# PLATFORM DETECTION
# ============================================================================

is_ci() {
    [ -n "${CI:-}" ] || \
    [ -n "${GITHUB_ACTIONS:-}" ] || \
    [ -n "${GITLAB_CI:-}" ]
}

is_ssh() {
    [ -n "${SSH_CONNECTION:-}" ] || \
    [ -n "${SSH_TTY:-}" ] || \
    [ -n "${SSH_CLIENT:-}" ]
}

# ============================================================================
# SOURCE CHANGE DETECTION
# ============================================================================

is_documentation_only() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1
    fi
    
    if [[ -z "$changed_files" ]]; then
        return 1
    fi
    
    # Check if ALL changed files are documentation
    local non_doc_files=$(echo "$changed_files" | \
        grep -v -E '\.(md|txt|rst|adoc|org)$' | \
        grep -v -E '^(docs/|documentation/|README|LICENSE|CHANGELOG|AUTHORS|CONTRIBUTORS|\.gitignore|\.gitattributes)' || true)
    
    [[ -z "$non_doc_files" ]]
}

has_source_changes() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1
    fi
    
    # Filter for actual source code files (not tests, not config, not docs)
    local code_files=$(echo "$changed_files" | grep -E '\.(rs|c|cpp|cc|cxx|h|hpp|hxx|py)$' || true)
    
    # Exclude test files but NOT utility directories (let coverage system handle what's coverable)
    # Updated pattern to exclude all test files including those in tracer_backend/tests/
    local source_files=$(echo "$code_files" | \
        grep -v -E '(^tests?/|/tests?/|_test\.|\.test\.|test_.*\.(cpp|c|h|rs)$|.*_test\.py$|/fixtures?/|/bench/|/benches/|build\.rs$)' | \
        grep -v -E '(tracer_backend/tests/)' | \
        grep -v -E '(^\.github/)' || true)
    
    if [[ -z "$source_files" ]]; then
        return 1
    else
        echo "$source_files"
        return 0
    fi
}

# ============================================================================
# BUILD PHASE
# ============================================================================

build_all() {
    start_timer "Build Phase"
    log_info "Building all components through Cargo..."

    local BUILD_FLAGS=""

    # Enable coverage instrumentation if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        BUILD_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        export RUSTFLAGS="-C instrument-coverage"
        export LLVM_PROFILE_FILE="${COVERAGE_DIR}/prof-%p-%m.profraw"
        mkdir -p "$COVERAGE_DIR"
    fi

    # Clean previous build artifacts
    if [[ "$MODE" != "incremental" ]]; then
        start_timer "Cargo Clean"
        cargo clean
        end_timer "Cargo Clean"
    fi

    # Run build and capture output
    start_timer "Cargo Build"
    if ! cargo build --all --release $BUILD_FLAGS 2>&1 | tee "$BUILD_OUTPUT"; then
        deduct_points 100 "Build failed"
        
        end_timer "Cargo Build"
        end_timer "Build Phase"
        return 1
    fi
    end_timer "Cargo Build"

    log_success "Build completed successfully"

    # Sign binaries on macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sign_test_binaries
    fi

    end_timer "Build Phase"
}

sign_test_binaries() {
    start_timer "Sign Test Binaries"
    log_info "Signing test binaries (required on macOS)..."

    # Skip on non-macOS platforms
    if [[ "$OSTYPE" != "darwin"* ]]; then
        log_info "Not macOS - skipping binary signing"
        end_timer "Sign Test Binaries"
        return 0
    fi

    if (is_ci || is_ssh) && [ -z "${APPLE_DEVELOPER_ID:-}" ]; then
        log_error "APPLE_DEVELOPER_ID not set - required for macOS development"
        deduct_points 100 "No Apple Developer certificate"
        end_timer "Sign Test Binaries"
        return 1
    fi

    # Use predictable paths from tracer_backend build.rs
    local test_dirs=""

    # Primary location: check release first (since build uses --release)
    if [ -d "${REPO_ROOT}/target/release/tracer_backend/test" ]; then
        test_dirs="${REPO_ROOT}/target/release/tracer_backend/test"
        log_info "Using optimized path: target/release/tracer_backend/test/"
        found_predictable=true
    # Also check debug directory
    elif [ -d "${REPO_ROOT}/target/debug/tracer_backend/test" ]; then
        test_dirs="${REPO_ROOT}/target/debug/tracer_backend/test"
        log_info "Using optimized path: target/debug/tracer_backend/test/"
        found_predictable=true
    fi

    # Fallback: search entire target directory (slower, may find duplicates)
    if [ "$found_predictable" = "false" ] || [ $(find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | wc -l | tr -d ' ') -eq 0 ]; then
        if [ "$found_predictable" = "true" ]; then
            log_info "Predictable test directory empty, falling back to full search"
        else
            log_info "Predictable test directory not found, falling back to full search"
        fi
        test_dirs="${REPO_ROOT}/target"
    fi

    # First count total binaries to sign
    local total_binaries=$(find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | wc -l | tr -d ' ')

    if [ "$total_binaries" -eq 0 ]; then
        log_info "No test binaries found to sign"
        rm -f "$count_file" "$error_file"
        return 0
    fi

    log_info "Found $total_binaries test binaries to sign..."

    # Use parallel signing for maximum performance
    local parallel_script="${REPO_ROOT}/utils/sign_binaries_parallel.sh"

    if [ -x "$parallel_script" ]; then
        # Use the optimized parallel signing script
        if "$parallel_script" "$test_dirs"; then
            sign_count=$total_binaries
            sign_failures=0
        else
            sign_failures=1  # At least one failed
        fi
    else
        # Fallback to sequential signing if parallel script not available
        log_warning "Parallel signing script not found, using sequential signing"

        find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | while IFS= read -r binary; do
            read sign_count sign_failures < "$count_file"
            sign_count=$((sign_count + 1))

            # Show progress
            if [ $((sign_count % 10)) -eq 0 ] || [ $((sign_count * 100 / total_binaries)) -gt $((((sign_count - 1) * 100 / total_binaries))) ]; then
                printf "\r${BLUE}[SIGNING]${NC} Progress: %d/%d (%.0f%%) - Failures: %d" \
                    "$sign_count" "$total_binaries" \
                    "$((sign_count * 100 / total_binaries))" \
                    "$sign_failures" >&2
            fi

            # Try fast signing script first, fallback to original
            local sign_script="${REPO_ROOT}/utils/sign_binary_fast.sh"
            [ ! -x "$sign_script" ] && sign_script="${REPO_ROOT}/utils/sign_binary.sh"

            local sign_output=$("$sign_script" "$binary" 2>&1)
            local sign_result=$?

            if [ $sign_result -ne 0 ]; then
                sign_failures=$((sign_failures + 1))
                echo "FAILED: $(basename "$binary")" >> "$error_file"
                echo "  Path: $binary" >> "$error_file"
                echo "  Error: $sign_output" | head -5 >> "$error_file"
                echo "---" >> "$error_file"
            fi

            echo "$sign_count $sign_failures" > "$count_file"
        done

        printf "\r%80s\r" " " >&2
        read sign_count sign_failures < "$count_file"
    fi

    if [ "$sign_failures" -gt 0 ]; then
        log_error "Failed to sign $sign_failures out of $sign_count binaries"

        # Show detailed error report
        if [ -f "$error_file" ] && [ -s "$error_file" ]; then
            echo "" >&2
            echo "${RED}=== Signing Failure Report ===${NC}" >&2
            cat "$error_file" >&2
            echo "${RED}==============================${NC}" >&2

            # Provide helpful suggestions based on common errors
            if grep -q "Developer ID" "$error_file" 2>/dev/null; then
                echo "" >&2
                echo "${YELLOW}Suggestion: Set APPLE_DEVELOPER_ID environment variable${NC}" >&2
                echo "  export APPLE_DEVELOPER_ID='Developer ID Application: Your Name'" >&2
            fi
            if grep -q "no identity found" "$error_file" 2>/dev/null; then
                echo "" >&2
                echo "${YELLOW}Suggestion: Check available certificates with:${NC}" >&2
                echo "  security find-identity -v -p codesigning" >&2
            fi
        fi

        rm -f "$count_file" "$error_file"
        deduct_points 100 "Binary signing failed"
        return 1
    fi

    # Note: count_file and error_file were used in the old sequential signing approach
    # They are no longer needed with parallel signing, but we keep success message

    log_success "Binary signing completed"

    end_timer "Sign Test Binaries"
    return 0
}

# ============================================================================
# TEST PHASE
# ============================================================================

run_tests() {
    start_timer "Test Phase"
    log_info "Running all tests through Cargo..."

    local TEST_FLAGS=""
    if [[ "$MODE" != "score-only" ]]; then
        TEST_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
    fi

    # Run tests and capture output
    start_timer "Cargo Test"
    if ! cargo test --all $TEST_FLAGS 2>&1 | tee "$TEST_OUTPUT"; then
        deduct_points 100 "Tests failed"

        end_timer "Cargo Test"
        end_timer "Test Phase"
        return 1
    fi
    end_timer "Cargo Test"

    log_success "All tests passed"
    end_timer "Test Phase"
}



# ============================================================================
# COVERAGE PHASE
# ============================================================================

collect_coverage() {
    if [[ "$MODE" == "score-only" ]]; then
        return 0
    fi

    start_timer "Coverage Phase"
    log_info "Collecting coverage data..."

    # Run coverage helper with timeout to prevent hanging
    # In incremental mode, use shorter timeout since we only care about changed files
    local coverage_timeout="300"  # 5 minutes default
    if [[ "$MODE" == "incremental" ]]; then
        coverage_timeout="60"  # 1 minute for incremental
    fi

    # The coverage_helper "full" command runs tests AGAIN with coverage enabled
    # This causes a rebuild. Since we already ran tests, just collect existing coverage data
    start_timer "Coverage Collection (timeout: ${coverage_timeout}s)"

    # Cross-platform timeout implementation
    # macOS doesn't have timeout command by default
    local cmd_output=$(mktemp)
    local cmd_status=$(mktemp)
    trap "rm -f $cmd_output $cmd_status" RETURN

    # Run command in background
    (
        cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- collect > "$cmd_output" 2>&1
        echo $? > "$cmd_status"
    ) &
    local cmd_pid=$!

    # Wait for command or timeout
    local elapsed=0
    while [[ $elapsed -lt $coverage_timeout ]] && kill -0 $cmd_pid 2>/dev/null; do
        sleep 1
        ((elapsed++))
    done

    if kill -0 $cmd_pid 2>/dev/null; then
        # Timeout reached, kill the process
        kill -TERM $cmd_pid 2>/dev/null
        sleep 1
        kill -KILL $cmd_pid 2>/dev/null
        wait $cmd_pid 2>/dev/null
        cat "$cmd_output"
        local exit_code=124  # Standard timeout exit code
    else
        # Command finished normally
        wait $cmd_pid
        cat "$cmd_output"
        local exit_code=$(cat "$cmd_status")
    fi

    if [[ $exit_code -ne 0 ]]; then
        if [[ $exit_code -eq 124 ]]; then
            end_timer "Coverage Collection (timeout: ${coverage_timeout}s)"
            log_warning "Coverage collection timed out after ${coverage_timeout}s"
            # In incremental mode, timeout is acceptable if tests passed
            if [[ "$MODE" == "incremental" ]]; then
                log_info "Proceeding without full coverage in incremental mode"
                end_timer "Coverage Phase"
                return 0
            fi
        else
            log_warning "Coverage collection failed"
        fi
        end_timer "Coverage Collection (timeout: ${coverage_timeout}s)"
        end_timer "Coverage Phase"
        return 1
    fi
    end_timer "Coverage Collection (timeout: ${coverage_timeout}s)"

    # Check incremental coverage for changed files
    if [[ "$MODE" == "incremental" ]]; then
        start_timer "Incremental Coverage Check"
        check_incremental_coverage
        end_timer "Incremental Coverage Check"
    fi

    end_timer "Coverage Phase"
}

check_incremental_coverage() {
    log_info "Checking incremental coverage for changed lines..."

    local changed_files=$(has_source_changes || echo "")
    if [[ -z "$changed_files" ]]; then
        log_info "No source files changed"
        return 0
    fi

    log_info "Changed source files:"
    echo "$changed_files" | sed 's/^/  - /'

    local merged_lcov="${COVERAGE_REPORT_DIR}/merged.lcov"
    if [[ ! -f "$merged_lcov" ]]; then
        log_error "No coverage data found"
        deduct_points 100 "Coverage data missing"
        return 1
    fi

    # Filter out test files from the LCOV report before passing to diff-cover
    # This ensures test files don't block commits due to coverage requirements
    local filtered_lcov="${COVERAGE_REPORT_DIR}/filtered.lcov"
    log_info "Filtering test files from coverage report..."

    # Copy the original LCOV file
    cp "$merged_lcov" "$filtered_lcov"

    # Remove test files using lcov's --remove option
    # This removes all test-related paths from the coverage data
    if command -v lcov >/dev/null 2>&1; then
        lcov --remove "$filtered_lcov" \
             "*/tests/*" \
             "*/test/*" \
             "*/bench/*" \
             "*/benchmark/*" \
             "*_test.c" \
             "*_test.cpp" \
             "*_test_support.c" \
             "*_test_support.cpp" \
             "*_whitebox.c" \
             "*_whitebox.cpp" \
             "*/googletest/*" \
             "*/gtest/*" \
             "*/_deps/googletest-*" \
             -o "$filtered_lcov" 2>/dev/null || {
            log_warn "Failed to filter test files with lcov, using original report"
            cp "$merged_lcov" "$filtered_lcov"
        }
    else
        log_warn "lcov not found, cannot filter test files from coverage"
    fi

    # Determine the comparison branch based on context
    local compare_branch="HEAD"  # Default: check uncommitted changes

    # If we have uncommitted changes, check them against HEAD
    # Otherwise check current commit against its parent
    if git diff --quiet && git diff --cached --quiet; then
        # No uncommitted changes, check current commit
        compare_branch="HEAD~1"
        log_info "No uncommitted changes, checking coverage for current commit"
    else
        # Have uncommitted changes, check them
        compare_branch="HEAD"
        log_info "Checking coverage for uncommitted changes"
    fi

    # For CI or when explicitly checking all branch changes
    if [[ "${CI:-false}" == "true" ]] || [[ "${CHECK_FULL_BRANCH:-false}" == "true" ]]; then
        if git rev-parse --verify origin/main &>/dev/null; then
            compare_branch="origin/main"
            log_info "CI mode: checking coverage against origin/main"
        fi
    fi

    # Get list of changed production files (excluding test files)
    # Use --cached to check only staged files when checking uncommitted changes
    local diff_command="git diff --name-only"
    if [[ "$compare_branch" == "HEAD" ]]; then
        diff_command="git diff --cached --name-only"
    fi
    local changed_files=$($diff_command "$compare_branch" | grep -E '\.(c|cpp|h|rs|py)$' | \
                          grep -v -E '(tests?/|bench/|benchmark/|_test\.|_whitebox\.|test_|_test_support\.)' || true)

    if [[ -z "$changed_files" ]]; then
        log_success "No production code changes to check"
        end_timer "Coverage Check"
        return 0
    fi

    log_info "Changed production files:"
    echo "$changed_files" | sed 's/^/  - /'

    # Run diff-cover on the filtered LCOV file
    # Capture output but don't immediately fail
    diff-cover "$filtered_lcov" \
               --fail-under=100 \
               --compare-branch="$compare_branch" 2>&1 | tee "$COVERAGE_OUTPUT"
    local diff_cover_result=${PIPESTATUS[0]}

    # Parse diff-cover output to check if only test files are missing coverage
    local production_files_missing_coverage=false
    local coverage_issues=""

    # Extract files with missing coverage from diff-cover output
    while IFS= read -r line; do
        # Match lines like "filename.c (XX.X%): Missing lines ..."
        if [[ "$line" =~ ^([^[:space:]]+\.(c|cpp|h|rs|py))[[:space:]]+\(([0-9.]+)%\):.*Missing ]]; then
            local file="${BASH_REMATCH[1]}"
            local coverage="${BASH_REMATCH[3]}"

            # Check if this is a test file
            if ! echo "$file" | grep -qE '(tests?/|bench/|benchmark/|_test\.|_whitebox\.|test_|_test_support\.)'; then
                # This is a production file with missing coverage
                production_files_missing_coverage=true
                coverage_issues="${coverage_issues}  - ${file} (${coverage}%)\n"
            else
                log_info "Ignoring test file coverage: ${file} (${coverage}%)"
            fi
        fi
    done < "$COVERAGE_OUTPUT"

    if [[ "$production_files_missing_coverage" == "true" ]]; then
        log_error "Production files lack coverage:"
        echo -e "$coverage_issues"
        deduct_points 100 "Production code must have 100% test coverage"
        return 1
    elif [[ $diff_cover_result -ne 0 ]]; then
        log_warning "Test files lack coverage (ignored for quality gate)"
    fi

    log_success "All changed lines have 100% coverage"
}



# ============================================================================
# REPORTING
# ============================================================================

generate_final_report() {
    # Human-readable report
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
    
    local score_color=$GREEN
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        score_color=$RED
    fi
    
    echo -e "Integration Score: ${score_color}${INTEGRATION_SCORE}/100${NC}"
    
    if [[ $INTEGRATION_SCORE -eq 100 ]]; then
        echo -e "${GREEN}✓ Quality Gate PASSED${NC}"
        return $CHECK_RESULT_PASSED
    else
        echo -e "${RED}✗ Quality Gate FAILED${NC}"
        return $CHECK_RESULT_BLOCKED
    fi
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

main() {
    log_info "Running quality gate in $MODE mode"
    
    # Check for documentation-only changes
    if [[ "$MODE" == "incremental" ]] && is_documentation_only; then
        log_success "Documentation-only changes detected"
        echo -e "${GREEN}Skipping quality checks for documentation${NC}"
        return $CHECK_RESULT_DOCUMENT_ONLY
    fi
    
    # Check for source changes
    local has_sources=true
    if [[ "$MODE" == "incremental" ]]; then
        local source_changes=$(has_source_changes || echo "")
        if [[ -z "$source_changes" ]]; then
            has_sources=false
            log_info "No source code changes - reduced checks"
        fi
    fi
    
    # Run quality checks
    build_all || true
    run_tests || true
    
    # Only check coverage if source files changed
    if [[ "$has_sources" == "true" ]] && [[ "$MODE" != "score-only" ]]; then
        collect_coverage || true
    fi
    
    # Generate final report
    generate_final_report
    local result=$?

    # Print timing summary
    print_timing_summary

    return $result
}

# Run main
main
EXIT_CODE=$?

# Clean exit
exit $EXIT_CODE