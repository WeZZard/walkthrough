# Third-Party Dependencies

This directory contains third-party SDK dependencies for the ADA project.

## Frida SDK

- **Version**: 17.2.16
- **Platform**: macos-arm64
- **Components**:
  - `frida-core/`: Frida Core SDK (for controller/tracer process)
  - `frida-gum/`: Frida Gum SDK (for agent/target process)

## Initialization

To download and extract the Frida SDKs, run:

```bash
../utils/init_third_parties.sh
```

## Directory Structure

```
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
├── frida-core-devkit-17.2.16-macos-arm64.tar.xz  # Archive (git-ignored)
└── frida-gum-devkit-17.2.16-macos-arm64.tar.xz   # Archive (git-ignored)
```

## Notes

- The `.tar.xz` archives are git-ignored to keep the repository size manageable
- The extracted directories contain the actual SDK files used during compilation
- Different team members may need different platform versions (macos-arm64, linux-x86_64, etc.)

## Updating Frida Version

To update to a new Frida version:

1. Edit `FRIDA_VERSION` in `../utils/init_third_parties.sh`
2. Delete the existing directories: `rm -rf frida-core frida-gum *.tar.xz`
3. Run the initialization script again

## Troubleshooting

- **Download failures**: Check your internet connection and GitHub access
- **Extraction failures**: Ensure `tar` with xz support is installed
- **Platform detection issues**: The script supports macOS and Linux on x86_64 and arm64
