#!/bin/bash
#
# Test: Target process exits normally
#
# Scenario: Start capture, target runs for 2 seconds, then exits with code 0.
# Expected: Session completes, recordings saved, status=complete.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

setup_test "target_exits_normally"

# Skip if ada-recorder not available
if [[ ! -f "$ADA_RECORDER" ]]; then
    skip "ada-recorder not built"
fi

# Start capture with voice only (faster, no screen permission needed)
echo "Starting capture..."
"$ADA" capture start "$TEST_TARGET" --no-screen --sleep 2 &
ADA_PID=$!

# Wait for ada to exit
if ! wait_for_process_exit $ADA_PID 30; then
    kill -9 $ADA_PID 2>/dev/null || true
    fail "ada did not exit within timeout"
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
assert_exit_code "$EXIT_CODE" 0 "ada exit code"
assert_file_exists "$SESSION_PATH/voice.wav" "voice.wav"
assert_file_not_empty "$SESSION_PATH/voice.wav" "voice.wav"
assert_manifest_exists "$SESSION_PATH"
assert_session_complete "$SESSION_PATH"

pass
