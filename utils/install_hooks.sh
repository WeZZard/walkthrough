#!/bin/bash
# Install git hooks for quality gate enforcement
# Per CLAUDE.md: Mandatory quality gates with no bypass

export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_DIR="${REPO_ROOT}/.git/hooks"
SCRIPTS_DIR="${REPO_ROOT}/utils"

echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}     Quality Gate Hooks Installation${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo ""

# Check if we're in a git repository
if [[ ! -d "${REPO_ROOT}/.git" ]]; then
    echo -e "${RED}[ERROR]${NC} Not in a git repository!"
    echo "Please run this script from the repository root."
    exit 1
fi

# Create hooks directory if it doesn't exist
mkdir -p "${HOOKS_DIR}"

# Function to install a hook
install_hook() {
    local hook_name=$1
    local source_file="${SCRIPTS_DIR}/hooks/${hook_name}"
    local target_file="${HOOKS_DIR}/${hook_name}"
    
    if [[ ! -f "$source_file" ]]; then
        echo -e "${RED}[ERROR]${NC} Hook source not found: $source_file"
        return 1
    fi
    
    # Backup existing hook if it exists
    if [[ -f "$target_file" ]] && [[ ! -L "$target_file" ]]; then
        echo -e "${YELLOW}[BACKUP]${NC} Backing up existing ${hook_name} to ${hook_name}.backup"
        mv "$target_file" "${target_file}.backup"
    fi
    
    # Create symlink to our hook
    ln -sf "$source_file" "$target_file"
    chmod +x "$source_file"
    
    echo -e "${GREEN}[✓]${NC} Installed ${hook_name} hook"
    return 0
}

# Install required dependencies check
check_dependencies() {
    echo -e "${BLUE}[CHECK]${NC} Verifying required tools..."
    
    local missing_deps=()
    
    # Check for required tools
    command -v cargo >/dev/null 2>&1 || missing_deps+=("cargo (Rust toolchain)")
    command -v git >/dev/null 2>&1 || missing_deps+=("git")
    
    # Check for coverage tools (optional but recommended)
    local coverage_tools_missing=false
    if ! command -v cargo-llvm-cov >/dev/null 2>&1; then
        echo -e "${YELLOW}[INFO]${NC} cargo-llvm-cov not found (recommended for Rust coverage)"
        echo -e "${YELLOW}      Install with: cargo install cargo-llvm-cov${NC}"
        coverage_tools_missing=true
    fi
    
    if ! command -v gcovr >/dev/null 2>&1 && ! command -v llvm-cov >/dev/null 2>&1; then
        echo -e "${YELLOW}[INFO]${NC} gcovr or llvm-cov not found (recommended for C/C++ coverage)"
        echo -e "${YELLOW}      Install gcovr with: pip install gcovr${NC}"
        coverage_tools_missing=true
    fi
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        echo -e "${RED}[ERROR]${NC} Missing required dependencies:"
        for dep in "${missing_deps[@]}"; do
            echo -e "${RED}  - $dep${NC}"
        done
        exit 1
    fi
    
    if [[ "$coverage_tools_missing" == "false" ]]; then
        echo -e "${GREEN}[✓]${NC} All required tools found"
    else
        echo -e "${YELLOW}[⚠]${NC} Some coverage tools missing (hooks will still work)"
    fi
}

# Main installation
main() {
    echo -e "${BLUE}[INFO]${NC} Installing quality gate hooks..."
    echo ""
    
    # Check dependencies
    check_dependencies
    echo ""
    
    # Install hooks
    echo -e "${BLUE}[INFO]${NC} Installing git hooks..."
    install_hook "pre-commit"
    install_hook "post-commit"
    echo ""
    
    # Make integration quality script executable
    chmod +x "${SCRIPTS_DIR}/metrics/integration_quality.sh"
    echo -e "${GREEN}[✓]${NC} Made integration_quality.sh executable"
    echo ""
    
    # Create initial coverage directories
    mkdir -p "${REPO_ROOT}/target/coverage/reports"
    echo -e "${GREEN}[✓]${NC} Created coverage directories"
    echo ""
    
    # Display configuration
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}     Installation Complete!${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${GREEN}Quality gates are now enforced with:${NC}"
    echo -e "  • Pre-commit: Blocks commits that fail critical gates"
    echo -e "  • Post-commit: Generates coverage reports in background"
    echo ""
    echo -e "${BLUE}Critical Quality Gates (100% required):${NC}"
    echo -e "  • Build must succeed"
    echo -e "  • All tests must pass"
    echo -e "  • No incomplete implementations (todo!/assert(0)/etc)"
    echo -e "  • Changed code must have ≥80% coverage"
    echo ""
    echo -e "${YELLOW}Important Notes:${NC}"
    echo -e "  • ${RED}NO bypassing with --no-verify${NC} (per CLAUDE.md)"
    echo -e "  • Coverage reports: target/coverage/reports/"
    echo -e "  • Run manually: ./utils/metrics/integration_quality.sh [mode]"
    echo -e "    Modes: full, incremental, score-only"
    echo ""
    echo -e "${GREEN}To test the hooks:${NC}"
    echo -e "  1. Make a change to any source file"
    echo -e "  2. Stage the change: git add <file>"
    echo -e "  3. Attempt commit: git commit -m \"test\""
    echo -e "  4. Hook will enforce quality gates"
    echo ""
    
    # Offer to run a test
    read -p "$(echo -e ${BLUE}Would you like to run a test of the quality gates now? [y/N]: ${NC})" -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${BLUE}[TEST]${NC} Running quality gate test..."
        "${SCRIPTS_DIR}/metrics/integration_quality.sh" score-only
    fi
}

# Run main
main "$@"