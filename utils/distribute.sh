#!/bin/bash
# Distribute ADA skills and binaries
#
# Creates distribution packages with path substitution for either:
# - standalone: Hardcoded paths to ADA-codex project location
# - plugin: Uses ${CLAUDE_PLUGIN_ROOT} variable for paths

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Parse arguments
FORM=""
OUTPUT_DIR="$PROJECT_ROOT/dist"

usage() {
    echo "Usage: $0 --form <plugin|standalone> [--output <dir>]"
    echo ""
    echo "Create distribution packages for ADA skills."
    echo ""
    echo "Options:"
    echo "  --form <plugin|standalone>  Required. Distribution form:"
    echo "                                standalone - Hardcode paths to ADA-codex project location"
    echo "                                plugin     - Use \${CLAUDE_PLUGIN_ROOT} variable for paths"
    echo "  --output <dir>              Output directory (default: ./dist)"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --form)
            FORM="$2"
            if [[ "$FORM" != "plugin" && "$FORM" != "standalone" ]]; then
                echo "Error: --form must be 'plugin' or 'standalone'"
                exit 1
            fi
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

[[ -z "$FORM" ]] && { echo "Error: --form is required"; usage; }

# Verify binaries exist
if [[ ! -f "$PROJECT_ROOT/target/release/ada" ]]; then
    echo "Error: ada binary not found at $PROJECT_ROOT/target/release/ada"
    echo "Run 'cargo build --release -p ada-cli' first."
    exit 1
fi

if [[ ! -f "$PROJECT_ROOT/target/release/tracer_backend/lib/libfrida_agent.dylib" ]]; then
    echo "Error: libfrida_agent.dylib not found"
    echo "Run 'cargo build --release' first."
    exit 1
fi

# Determine path prefix for substitution
if [[ "$FORM" == "standalone" ]]; then
    ADA_ROOT="$PROJECT_ROOT/target/release"
    ADA_LIB_DIR="$PROJECT_ROOT/target/release/tracer_backend/lib"
    ADA_BIN_DIR="$PROJECT_ROOT/target/release"
else
    ADA_ROOT="\${CLAUDE_PLUGIN_ROOT}"
    ADA_LIB_DIR="\${CLAUDE_PLUGIN_ROOT}/lib"
    ADA_BIN_DIR="\${CLAUDE_PLUGIN_ROOT}/bin"
fi

# Create output structure
rm -rf "$OUTPUT_DIR"

if [[ "$FORM" == "plugin" ]]; then
    mkdir -p "$OUTPUT_DIR"/{.claude-plugin,bin,lib,skills/run,skills/analyze}

    # Get version from workspace root Cargo.toml
    VERSION=$(grep -A1 '^\[workspace\.package\]' "$PROJECT_ROOT/Cargo.toml" | grep '^version' | sed 's/.*"\(.*\)".*/\1/')
    [[ -z "$VERSION" ]] && VERSION="0.1.0"

    # Generate plugin.json with marketplace-compatible fields
    cat > "$OUTPUT_DIR/.claude-plugin/plugin.json" << EOF
{
  "name": "ada",
  "version": "$VERSION",
  "description": "ADA - AI-powered application debugging assistant with dual-lane flight recorder",
  "author": {
    "name": "WeZZard",
    "url": "https://github.com/WeZZard"
  },
  "repository": "https://github.com/agentic-infra/ada",
  "homepage": "https://github.com/agentic-infra/ada",
  "license": "MIT",
  "keywords": [
    "debugging",
    "tracing",
    "profiling",
    "frida",
    "macos"
  ]
}
EOF

    # Copy binaries
    cp "$PROJECT_ROOT/target/release/ada" "$OUTPUT_DIR/bin/"
    chmod +x "$OUTPUT_DIR/bin/ada"

    # Copy ada-capture-daemon if it exists
    if [[ -f "$PROJECT_ROOT/target/release/ada-capture-daemon" ]]; then
        cp "$PROJECT_ROOT/target/release/ada-capture-daemon" "$OUTPUT_DIR/bin/"
        chmod +x "$OUTPUT_DIR/bin/ada-capture-daemon"
    fi

    # Copy library
    cp "$PROJECT_ROOT/target/release/tracer_backend/lib/libfrida_agent.dylib" "$OUTPUT_DIR/lib/"

    # Copy and substitute skills
    for skill in run analyze; do
        if [[ -f "$PROJECT_ROOT/claude/skills/$skill/SKILL.md" ]]; then
            sed -e "s|\${ADA_ROOT}|$ADA_ROOT|g" \
                -e "s|\${ADA_LIB_DIR}|$ADA_LIB_DIR|g" \
                -e "s|\${ADA_BIN_DIR}|$ADA_BIN_DIR|g" \
                "$PROJECT_ROOT/claude/skills/$skill/SKILL.md" \
                > "$OUTPUT_DIR/skills/$skill/SKILL.md"
        fi
    done

    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: plugin"
    echo "ADA_ROOT: $ADA_ROOT"
    echo ""
    echo "Contents:"
    find "$OUTPUT_DIR" -type f | sort | sed 's|^|  |'

else
    # Standalone: just command files with hardcoded paths
    mkdir -p "$OUTPUT_DIR"

    for skill in run analyze; do
        if [[ -f "$PROJECT_ROOT/claude/skills/$skill/SKILL.md" ]]; then
            sed -e "s|\${ADA_ROOT}|$ADA_ROOT|g" \
                -e "s|\${ADA_LIB_DIR}|$ADA_LIB_DIR|g" \
                -e "s|\${ADA_BIN_DIR}|$ADA_BIN_DIR|g" \
                "$PROJECT_ROOT/claude/skills/$skill/SKILL.md" \
                > "$OUTPUT_DIR/$skill.md"
        fi
    done

    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: standalone"
    echo "ADA_ROOT: $ADA_ROOT"
    echo ""
    echo "Files:"
    ls -la "$OUTPUT_DIR"/*.md 2>/dev/null || echo "  (no files)"
fi
