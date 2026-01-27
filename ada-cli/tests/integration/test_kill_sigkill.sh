#!/bin/bash
#
# Test: Target killed with SIGKILL (kill -9)
#
# Scenario: Start capture with hanging target, send SIGKILL to target.
# Expected: Ada detects target exit, session completes.
# Note: This simulates Activity Monitor "Force Quit".
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

setup_test "target_killed_sigkill"

# Skip if ada-recorder not available
if [[ ! -f "$ADA_RECORDER" ]]; then
    skip "ada-recorder not built"
fi

# Start capture with hanging target
echo "Starting capture with hanging target..."
"$ADA" capture start "$TEST_TARGET" --no-screen -- --hang &
ADA_PID=$!

# Give it time to start
sleep 2

# Find the target process
TARGET_PID=$(pgrep -P $ADA_PID test-target 2>/dev/null || true)
if [[ -z "$TARGET_PID" ]]; then
    TARGET_PID=$(pgrep -n test-target 2>/dev/null || true)
fi

if [[ -z "$TARGET_PID" ]]; then
    kill -INT $ADA_PID 2>/dev/null || true
    skip "Could not find target process PID"
fi

echo "Target PID: $TARGET_PID"

# Send SIGKILL to target (force kill)
echo "Sending SIGKILL to target (PID $TARGET_PID)..."
kill -9 $TARGET_PID

# Wait for ada to exit
if ! wait_for_process_exit $ADA_PID 30; then
    kill -9 $ADA_PID 2>/dev/null || true
    fail "ada did not exit within timeout after target SIGKILL"
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

# Assertions
assert_file_exists "$SESSION_PATH/voice.wav" "voice.wav"
assert_manifest_exists "$SESSION_PATH"
assert_session_complete "$SESSION_PATH"

pass
