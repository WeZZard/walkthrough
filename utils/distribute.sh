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
BUILD_PROFILE="release"
DO_BUILD=true  # Build by default (incremental, so cheap if nothing changed)

usage() {
    echo "Usage: $0 --form <plugin|standalone> [options]"
    echo ""
    echo "Create distribution packages for ADA skills."
    echo ""
    echo "Builds binaries by default (incremental - fast if nothing changed)."
    echo ""
    echo "Options:"
    echo "  --form <plugin|standalone>  Required. Distribution form:"
    echo "                                standalone - Hardcode paths to ADA-codex project location"
    echo "                                plugin     - Use \${CLAUDE_PLUGIN_ROOT} variable for paths"
    echo "  --output <dir>              Output directory (default: ./dist)"
    echo "  --debug                     Use debug build instead of release"
    echo "  --no-build                  Skip build, use existing binaries"
    echo "  -h, --help                  Show this help message"
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
        --debug)
            BUILD_PROFILE="debug"
            shift
            ;;
        --no-build)
            DO_BUILD=false
            shift
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

# Set target directory based on profile
TARGET_DIR="$PROJECT_ROOT/target/$BUILD_PROFILE"

# Build if requested
if [[ "$DO_BUILD" == true ]]; then
    echo "Building ADA ($BUILD_PROFILE profile)..."

    if [[ "$BUILD_PROFILE" == "release" ]]; then
        cargo build --release -p ada-cli
    else
        cargo build -p ada-cli
    fi

    # Build ada-recorder (Swift)
    echo "Building ada-recorder..."
    pushd "$PROJECT_ROOT/ada-recorder/macos" > /dev/null
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        swift build -c release
        cp .build/release/ada-recorder "$TARGET_DIR/"
    else
        swift build
        cp .build/debug/ada-recorder "$TARGET_DIR/"
    fi
    popd > /dev/null

    echo "Build complete."
fi

# Verify binaries exist
if [[ ! -f "$TARGET_DIR/ada" ]]; then
    echo "Error: ada binary not found at $TARGET_DIR/ada"
    echo "Run with --build flag or manually build first:"
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        echo "  cargo build --release -p ada-cli"
    else
        echo "  cargo build -p ada-cli"
    fi
    exit 1
fi

if [[ ! -f "$TARGET_DIR/tracer_backend/lib/libfrida_agent.dylib" ]]; then
    echo "Error: libfrida_agent.dylib not found"
    echo "Run with --build flag or manually build first:"
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        echo "  cargo build --release"
    else
        echo "  cargo build"
    fi
    exit 1
fi

# Determine path prefix for substitution
if [[ "$FORM" == "standalone" ]]; then
    ADA_ROOT="$TARGET_DIR"
    ADA_LIB_DIR="$TARGET_DIR/tracer_backend/lib"
    ADA_BIN_DIR="$TARGET_DIR"
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
    cp "$TARGET_DIR/ada" "$OUTPUT_DIR/bin/"
    chmod +x "$OUTPUT_DIR/bin/ada"

    # Copy ada-recorder if it exists
    if [[ -f "$TARGET_DIR/ada-recorder" ]]; then
        cp "$TARGET_DIR/ada-recorder" "$OUTPUT_DIR/bin/"
        chmod +x "$OUTPUT_DIR/bin/ada-recorder"
    else
        echo "Warning: ada-recorder not found at $TARGET_DIR/ada-recorder"
    fi

    # Copy ada-capture-daemon if it exists
    if [[ -f "$TARGET_DIR/ada-capture-daemon" ]]; then
        cp "$TARGET_DIR/ada-capture-daemon" "$OUTPUT_DIR/bin/"
        chmod +x "$OUTPUT_DIR/bin/ada-capture-daemon"
    fi

    # Copy library
    cp "$TARGET_DIR/tracer_backend/lib/libfrida_agent.dylib" "$OUTPUT_DIR/lib/"

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

    echo ""
    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: plugin"
    echo "Profile: $BUILD_PROFILE"
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

    echo ""
    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: standalone"
    echo "Profile: $BUILD_PROFILE"
    echo "ADA_ROOT: $ADA_ROOT"
    echo ""
    echo "Files:"
    ls -la "$OUTPUT_DIR"/*.md 2>/dev/null || echo "  (no files)"
fi
