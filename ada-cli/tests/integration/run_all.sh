#!/bin/bash
#
# Run all ada capture integration tests.
#
# Usage:
#   ./run_all.sh           # Run all tests
#   ./run_all.sh test_foo  # Run specific test
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0

run_test() {
    local test_script="$1"
    local test_name=$(basename "$test_script" .sh)

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    if bash "$test_script"; then
        PASSED=$((PASSED + 1))
    else
        local exit_code=$?
        if [[ $exit_code -eq 77 ]]; then
            # Exit code 77 means skip (common convention)
            SKIPPED=$((SKIPPED + 1))
        else
            FAILED=$((FAILED + 1))
        fi
    fi
}

main() {
    echo "ADA Capture Integration Tests"
    echo "=============================="
    echo ""
    echo "Project: $PROJECT_ROOT"
    echo "ADA:     $ADA"
    echo ""

    # Ensure prerequisites
    ensure_ada_built
    ensure_ada_recorder_built
    build_test_target

    # Collect tests
    local tests=()
    if [[ $# -gt 0 ]]; then
        # Run specific tests
        for arg in "$@"; do
            if [[ -f "$SCRIPT_DIR/$arg" ]]; then
                tests+=("$SCRIPT_DIR/$arg")
            elif [[ -f "$SCRIPT_DIR/test_$arg.sh" ]]; then
                tests+=("$SCRIPT_DIR/test_$arg.sh")
            elif [[ -f "$SCRIPT_DIR/${arg}.sh" ]]; then
                tests+=("$SCRIPT_DIR/${arg}.sh")
            else
                echo -e "${RED}Test not found: $arg${NC}"
                exit 1
            fi
        done
    else
        # Run all test_*.sh files
        for test_script in "$SCRIPT_DIR"/test_*.sh; do
            if [[ -f "$test_script" ]]; then
                tests+=("$test_script")
            fi
        done
    fi

    if [[ ${#tests[@]} -eq 0 ]]; then
        echo "No tests found."
        exit 0
    fi

    echo "Running ${#tests[@]} test(s)..."

    # Run tests
    for test_script in "${tests[@]}"; do
        run_test "$test_script"
    done

    # Summary
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Summary"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "  ${GREEN}Passed:  $PASSED${NC}"
    echo -e "  ${RED}Failed:  $FAILED${NC}"
    echo -e "  ${YELLOW}Skipped: $SKIPPED${NC}"
    echo ""

    if [[ $FAILED -gt 0 ]]; then
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    else
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    fi
}

main "$@"
