#!/bin/bash
#
# Test: Target crashes with SIGSEGV
#
# Scenario: Start capture with target that crashes.
# Expected: Ada detects crash, session completes.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

setup_test "target_crashes_sigsegv"

# Skip if ada-recorder not available
if [[ ! -f "$ADA_RECORDER" ]]; then
    skip "ada-recorder not built"
fi

# Start capture with crashing target
echo "Starting capture with crashing target..."
"$ADA" capture start "$TEST_TARGET" --no-screen -- --crash &
ADA_PID=$!

# Wait for ada to exit (should happen quickly after crash)
if ! wait_for_process_exit $ADA_PID 30; then
    kill -9 $ADA_PID 2>/dev/null || true
    fail "ada did not exit within timeout after target crash"
fi

# Get exit code
wait $ADA_PID || EXIT_CODE=$?
EXIT_CODE=${EXIT_CODE:-0}

# Find the session
SESSION_ID=$(find_latest_session)
if [[ -z "$SESSION_ID" ]]; then
    fail "No session found"
fi
SESSION_PATH=$(get_session_path "$SESSION_ID")

echo "Session: $SESSION_ID"
echo "Path: $SESSION_PATH"

# Assertions - note: voice file might be very short due to quick crash
assert_file_exists "$SESSION_PATH/voice.wav" "voice.wav"
assert_manifest_exists "$SESSION_PATH"
assert_session_complete "$SESSION_PATH"

pass
