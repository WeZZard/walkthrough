#!/bin/bash
# Initialize third-party dependencies for ADA project
# This script downloads and extracts Frida SDK components

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
FRIDA_VERSION="17.2.16"
THIRD_PARTIES_DIR="third_parties"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Platform detection
detect_platform() {
    local os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local arch=$(uname -m)
    
    case "$os" in
        darwin)
            os="macos"
            ;;
        linux)
            os="linux"
            ;;
        *)
            echo -e "${RED}Error: Unsupported OS: $os${NC}"
            exit 1
            ;;
    esac
    
    case "$arch" in
        x86_64)
            arch="x86_64"
            ;;
        arm64|aarch64)
            arch="arm64"
            ;;
        *)
            echo -e "${RED}Error: Unsupported architecture: $arch${NC}"
            exit 1
            ;;
    esac
    
    echo "${os}-${arch}"
}

# Function to download a file if it doesn't exist
download_if_needed() {
    local url="$1"
    local output_file="$2"
    
    if [ -f "$output_file" ]; then
        echo -e "${YELLOW}File already exists: $output_file${NC}"
        return 0
    fi
    
    echo -e "${BLUE}Downloading: $url${NC}"
    if command -v curl &> /dev/null; then
        curl -L -o "$output_file" "$url" || {
            echo -e "${RED}Failed to download $url${NC}"
            rm -f "$output_file"
            return 1
        }
    elif command -v wget &> /dev/null; then
        wget -O "$output_file" "$url" || {
            echo -e "${RED}Failed to download $url${NC}"
            rm -f "$output_file"
            return 1
        }
    else
        echo -e "${RED}Error: Neither curl nor wget is available${NC}"
        return 1
    fi
    
    echo -e "${GREEN}Downloaded: $output_file${NC}"
}

# Function to extract tarball
extract_tarball() {
    local tarball="$1"
    local target_dir="$2"
    
    echo -e "${BLUE}Extracting: $tarball to $target_dir${NC}"
    
    # Create target directory
    mkdir -p "$target_dir"
    
    # Extract based on file extension
    if [[ "$tarball" == *.tar.xz ]]; then
        tar -xJf "$tarball" -C "$target_dir" || {
            echo -e "${RED}Failed to extract $tarball${NC}"
            return 1
        }
    elif [[ "$tarball" == *.tar.gz ]]; then
        tar -xzf "$tarball" -C "$target_dir" || {
            echo -e "${RED}Failed to extract $tarball${NC}"
            return 1
        }
    else
        echo -e "${RED}Unknown archive format: $tarball${NC}"
        return 1
    fi
    
    echo -e "${GREEN}Extracted: $target_dir${NC}"
}

# Function to verify extraction
verify_extraction() {
    local dir="$1"
    local expected_files=("$@")
    
    # Skip first argument (directory)
    expected_files=("${expected_files[@]:1}")
    
    for file in "${expected_files[@]}"; do
        if [ ! -f "$dir/$file" ]; then
            echo -e "${RED}Missing expected file: $dir/$file${NC}"
            return 1
        fi
    done
    
    return 0
}

# Main function
main() {
    echo -e "${GREEN}=== ADA Third-Party Dependencies Initialization ===${NC}"
    echo -e "${BLUE}Project root: $PROJECT_ROOT${NC}"
    
    # Detect platform
    PLATFORM=$(detect_platform)
    echo -e "${BLUE}Detected platform: $PLATFORM${NC}"
    
    # Create third_parties directory
    cd "$PROJECT_ROOT"
    mkdir -p "$THIRD_PARTIES_DIR"
    cd "$THIRD_PARTIES_DIR"
    
    # Define Frida SDK URLs and files
    FRIDA_CORE_ARCHIVE="frida-core-devkit-${FRIDA_VERSION}-${PLATFORM}.tar.xz"
    FRIDA_GUM_ARCHIVE="frida-gum-devkit-${FRIDA_VERSION}-${PLATFORM}.tar.xz"
    
    FRIDA_CORE_URL="https://github.com/frida/frida/releases/download/${FRIDA_VERSION}/${FRIDA_CORE_ARCHIVE}"
    FRIDA_GUM_URL="https://github.com/frida/frida/releases/download/${FRIDA_VERSION}/${FRIDA_GUM_ARCHIVE}"
    
    echo -e "${GREEN}=== Downloading Frida SDKs ===${NC}"
    
    # Download Frida Core SDK
    download_if_needed "$FRIDA_CORE_URL" "$FRIDA_CORE_ARCHIVE" || exit 1
    
    # Download Frida Gum SDK
    download_if_needed "$FRIDA_GUM_URL" "$FRIDA_GUM_ARCHIVE" || exit 1
    
    echo -e "${GREEN}=== Extracting Frida SDKs ===${NC}"
    
    # Extract Frida Core SDK
    if [ ! -d "frida-core" ] || [ ! -f "frida-core/libfrida-core.a" ]; then
        rm -rf frida-core
        extract_tarball "$FRIDA_CORE_ARCHIVE" "frida-core" || exit 1
        
        # Verify extraction
        verify_extraction "frida-core" "libfrida-core.a" "frida-core.h" || {
            echo -e "${RED}Frida Core extraction verification failed${NC}"
            exit 1
        }
    else
        echo -e "${YELLOW}Frida Core already extracted${NC}"
    fi
    
    # Extract Frida Gum SDK
    if [ ! -d "frida-gum" ] || [ ! -f "frida-gum/libfrida-gum.a" ]; then
        rm -rf frida-gum
        extract_tarball "$FRIDA_GUM_ARCHIVE" "frida-gum" || exit 1
        
        # Verify extraction
        verify_extraction "frida-gum" "libfrida-gum.a" "frida-gum.h" || {
            echo -e "${RED}Frida Gum extraction verification failed${NC}"
            exit 1
        }
    else
        echo -e "${YELLOW}Frida Gum already extracted${NC}"
    fi
    
    echo -e "${GREEN}=== Creating README ===${NC}"
    
    # Create README for third_parties directory
    cat > README.md << EOF
# Third-Party Dependencies

This directory contains third-party SDK dependencies for the ADA project.

## Frida SDK

- **Version**: ${FRIDA_VERSION}
- **Platform**: ${PLATFORM}
- **Components**:
  - \`frida-core/\`: Frida Core SDK (for controller/tracer process)
  - \`frida-gum/\`: Frida Gum SDK (for agent/target process)

## Initialization

To download and extract the Frida SDKs, run:
\`\`\`bash
../utils/init_third_parties.sh
\`\`\`

## Directory Structure

\`\`\`
third_parties/
├── README.md                                          # This file
├── frida-core/                                       # Extracted Frida Core SDK
│   ├── libfrida-core.a                              # Static library
│   ├── frida-core.h                                 # Headers
│   └── ...
├── frida-gum/                                        # Extracted Frida Gum SDK
│   ├── libfrida-gum.a                               # Static library
│   ├── frida-gum.h                                  # Headers
│   └── ...
├── frida-core-devkit-${FRIDA_VERSION}-${PLATFORM}.tar.xz  # Archive (git-ignored)
└── frida-gum-devkit-${FRIDA_VERSION}-${PLATFORM}.tar.xz   # Archive (git-ignored)
\`\`\`

## Notes

- The \`.tar.xz\` archives are git-ignored to keep the repository size manageable
- The extracted directories contain the actual SDK files used during compilation
- Different team members may need different platform versions (macos-arm64, linux-x86_64, etc.)

## Updating Frida Version

To update to a new Frida version:
1. Edit \`FRIDA_VERSION\` in \`../utils/init_third_parties.sh\`
2. Delete the existing directories: \`rm -rf frida-core frida-gum *.tar.xz\`
3. Run the initialization script again

## Troubleshooting

- **Download failures**: Check your internet connection and GitHub access
- **Extraction failures**: Ensure \`tar\` with xz support is installed
- **Platform detection issues**: The script supports macOS and Linux on x86_64 and arm64
EOF
    
    echo -e "${GREEN}=== Summary ===${NC}"
    echo -e "${GREEN}Third-party dependencies initialized successfully!${NC}"
    echo ""
    echo "Frida SDK components:"
    echo "  - frida-core: $(ls -lh frida-core/libfrida-core.a 2>/dev/null | awk '{print $5}' || echo 'N/A')"
    echo "  - frida-gum:  $(ls -lh frida-gum/libfrida-gum.a 2>/dev/null | awk '{print $5}' || echo 'N/A')"
    echo ""
    echo -e "${BLUE}You can now use these SDKs in your CMake/Cargo build configuration.${NC}"
}

# Run main function
main "$@"