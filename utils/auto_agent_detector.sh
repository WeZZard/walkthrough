#!/bin/bash
# Auto-detect which agent to invoke based on current context

# Function to detect project language
detect_language() {
    if [ -f "Cargo.toml" ]; then
        echo "rust"
    elif [ -f "pyproject.toml" ] || [ -f "requirements.txt" ]; then
        echo "python"
    elif [ -f "CMakeLists.txt" ] || [ -f "Makefile" ]; then
        echo "cpp"
    elif [ -f "package.json" ]; then
        echo "javascript"
    else
        echo "unknown"
    fi
}

# Function to detect current task type
detect_task() {
    local current_branch=$(git branch --show-current 2>/dev/null)
    local recent_files=$(git diff --name-only HEAD~1 2>/dev/null)
    
    # Check for planning documents
    if echo "$recent_files" | grep -q "progress_trackings\|DESIGN\|ARCHITECTURE"; then
        echo "planning"
    # Check for test files
    elif echo "$recent_files" | grep -q "test\|spec"; then
        echo "testing"
    # Check for PR/commit context
    elif [ -n "$current_branch" ] && [ "$current_branch" != "main" ]; then
        echo "integration"
    else
        echo "development"
    fi
}

# Main detection logic
LANGUAGE=$(detect_language)
TASK=$(detect_task)

echo "Detected context:"
echo "  Language: $LANGUAGE"
echo "  Task: $TASK"
echo ""

# Recommend agent
case "$TASK" in
    planning)
        echo "Recommended agent: @iteration-planner"
        echo "Agent hours estimate: 0.5-2 AH"
        ;;
    testing)
        echo "Recommended agent: @${LANGUAGE}-developer (test focus)"
        echo "Agent hours estimate: 0.5-1.5 AH"
        ;;
    integration)
        echo "Recommended agent: @integration-engineer"
        echo "Agent hours estimate: 0.25-1 AH"
        ;;
    development)
        echo "Recommended agent: @${LANGUAGE}-developer"
        echo "Agent hours estimate: 1-4 AH"
        ;;
    *)
        echo "Recommended agent: @architect (for initial design)"
        echo "Agent hours estimate: 1-3 AH"
        ;;
esac

echo ""
echo "To invoke: Ask Claude to use the recommended agent for your task"