#!/bin/bash
#
# Common utilities for ada capture integration tests.
#

set -euo pipefail

# Paths - can be overridden via environment
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../../.." && pwd)}"
ADA="${ADA:-$PROJECT_ROOT/target/release/ada}"
ADA_RECORDER="${ADA_RECORDER:-$PROJECT_ROOT/target/release/ada-recorder}"
TEST_TARGET="${TEST_TARGET:-$PROJECT_ROOT/target/release/test-target}"
FIXTURES_DIR="$SCRIPT_DIR/../fixtures"

# Test state
SESSION_DIR=""
TEST_NAME=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

#
# Setup and teardown
#

setup_test() {
    TEST_NAME="$1"
    SESSION_DIR=$(mktemp -d)
    export ADA_SESSION_DIR="$SESSION_DIR"
    echo -e "${YELLOW}Running: $TEST_NAME${NC}"
}

cleanup_test() {
    if [[ -n "$SESSION_DIR" && -d "$SESSION_DIR" ]]; then
        rm -rf "$SESSION_DIR"
    fi
    SESSION_DIR=""
}

#
# Build helpers
#

build_test_target() {
    if [[ ! -f "$TEST_TARGET" ]]; then
        echo "Building test-target..."
        cc -o "$TEST_TARGET" "$FIXTURES_DIR/test-target.c"
    fi
}

ensure_ada_built() {
    if [[ ! -f "$ADA" ]]; then
        echo -e "${RED}Error: ada binary not found at $ADA${NC}"
        echo "Run: cargo build --release -p ada-cli"
        exit 1
    fi
}

ensure_ada_recorder_built() {
    if [[ ! -f "$ADA_RECORDER" ]]; then
        echo -e "${YELLOW}Warning: ada-recorder not found at $ADA_RECORDER${NC}"
        echo "Run: cd ada-recorder/macos && swift build -c release && cp .build/release/ada-recorder $PROJECT_ROOT/target/release/"
    fi
}

#
# Assertion helpers
#

assert_file_exists() {
    local path="$1"
    local description="${2:-file}"

    if [[ -f "$path" ]]; then
        local size=$(stat -f%z "$path" 2>/dev/null || stat --printf="%s" "$path" 2>/dev/null || echo "unknown")
        echo "  ✓ $description exists ($size bytes)"
        return 0
    else
        echo -e "  ${RED}✗ $description not found at: $path${NC}"
        return 1
    fi
}

assert_file_not_empty() {
    local path="$1"
    local description="${2:-file}"

    if [[ -f "$path" && -s "$path" ]]; then
        local size=$(stat -f%z "$path" 2>/dev/null || stat --printf="%s" "$path" 2>/dev/null || echo "unknown")
        echo "  ✓ $description is not empty ($size bytes)"
        return 0
    else
        echo -e "  ${RED}✗ $description is empty or missing: $path${NC}"
        return 1
    fi
}

assert_exit_code() {
    local actual="$1"
    local expected="$2"
    local description="${3:-exit code}"

    if [[ "$actual" -eq "$expected" ]]; then
        echo "  ✓ $description is $expected"
        return 0
    else
        echo -e "  ${RED}✗ $description: expected $expected, got $actual${NC}"
        return 1
    fi
}

assert_session_complete() {
    local session_path="$1"
    local session_json="$session_path/session.json"

    if [[ ! -f "$session_json" ]]; then
        echo -e "  ${RED}✗ session.json not found${NC}"
        return 1
    fi

    if grep -q '"status"[[:space:]]*:[[:space:]]*"complete"' "$session_json"; then
        echo "  ✓ session status is complete"
        return 0
    else
        local status=$(grep '"status"' "$session_json" || echo "unknown")
        echo -e "  ${RED}✗ session not complete: $status${NC}"
        return 1
    fi
}

assert_manifest_exists() {
    local session_path="$1"
    local manifest="$session_path/manifest.json"

    if [[ -f "$manifest" ]]; then
        echo "  ✓ manifest.json exists"
        return 0
    else
        echo -e "  ${RED}✗ manifest.json not found${NC}"
        return 1
    fi
}

#
# Test result helpers
#

pass() {
    echo -e "${GREEN}PASS: $TEST_NAME${NC}"
    cleanup_test
}

fail() {
    local reason="${1:-assertion failed}"
    echo -e "${RED}FAIL: $TEST_NAME - $reason${NC}"
    cleanup_test
    exit 1
}

skip() {
    local reason="${1:-skipped}"
    echo -e "${YELLOW}SKIP: $TEST_NAME - $reason${NC}"
    cleanup_test
    exit 0
}

#
# Session helpers
#

find_latest_session() {
    local sessions_dir="$HOME/.ada/sessions"
    if [[ -d "$sessions_dir" ]]; then
        ls -1t "$sessions_dir" | head -1
    fi
}

get_session_path() {
    local session_id="$1"
    echo "$HOME/.ada/sessions/$session_id"
}

wait_for_session_complete() {
    local session_id="$1"
    local timeout="${2:-30}"
    local session_path=$(get_session_path "$session_id")
    local elapsed=0

    while [[ $elapsed -lt $timeout ]]; do
        if [[ -f "$session_path/session.json" ]]; then
            if grep -q '"status"[[:space:]]*:[[:space:]]*"complete"' "$session_path/session.json"; then
                return 0
            fi
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    return 1
}

#
# Process helpers
#

wait_for_process_exit() {
    local pid="$1"
    local timeout="${2:-30}"
    local elapsed=0

    while kill -0 "$pid" 2>/dev/null && [[ $elapsed -lt $timeout ]]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$pid" 2>/dev/null; then
        return 1  # Process still running
    fi
    return 0
}

# Export functions for use in test scripts
export -f setup_test cleanup_test build_test_target ensure_ada_built ensure_ada_recorder_built
export -f assert_file_exists assert_file_not_empty assert_exit_code assert_session_complete assert_manifest_exists
export -f pass fail skip find_latest_session get_session_path wait_for_session_complete wait_for_process_exit
