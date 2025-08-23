#!/bin/bash
# Setup environment for coverage collection

# Add LLVM tools to PATH
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Find and export LLVM tools from rustup
export RUSTUP_HOME="${HOME}/.rustup"

# Find the LLVM tools in the Rust toolchain
if [[ -z "${LLVM_COV:-}" ]]; then
    LLVM_COV=$(find ${RUSTUP_HOME} -name llvm-cov -type f 2>/dev/null | head -1)
    if [[ -n "$LLVM_COV" ]]; then
        export LLVM_COV
        echo "Found LLVM_COV: $LLVM_COV"
    fi
fi

if [[ -z "${LLVM_PROFDATA:-}" ]]; then
    LLVM_PROFDATA=$(find ${RUSTUP_HOME} -name llvm-profdata -type f 2>/dev/null | head -1)
    if [[ -n "$LLVM_PROFDATA" ]]; then
        export LLVM_PROFDATA
        echo "Found LLVM_PROFDATA: $LLVM_PROFDATA"
    fi
fi

# Alternative: Use brew-installed LLVM if available
if command -v llvm-cov >/dev/null 2>&1; then
    export LLVM_COV_BREW=$(which llvm-cov)
    export LLVM_PROFDATA_BREW=$(which llvm-profdata)
    echo "Brew LLVM tools available: $LLVM_COV_BREW"
fi

echo "Coverage environment configured."
echo "You can now run: cargo llvm-cov"