---
id: CN-001
title: Platform Limitations and Restrictions
status: active
source: docs/specs/PLATFORM_LIMITATIONS.md
---

# Platform Limitations and Restrictions

## Context

**Given:**
- ADA is a multi-platform debugging tool operating in security-constrained environments
- Different operating systems impose different restrictions on debugging capabilities
- System binaries and protected processes have special protections
- Users need clear guidance on what can and cannot be traced

## Trigger

**When:** A user attempts to trace a binary or process on a specific platform

## Outcome

**Then:**
- Platform-specific limitations are clearly communicated
- The system detects protected binaries before attempting operations
- Error messages distinguish between fixable and unfixable constraints
- Users receive actionable guidance appropriate for their situation
- The system never suggests security-compromising workarounds

## macOS Limitations

### System Binary Restrictions
**Given:** System Integrity Protection (SIP) is enabled on macOS
**When:** A user attempts to trace a system binary
**Then:**
- The operation is blocked before execution
- Error message clearly states: "Cannot trace system binary on macOS"
- Explanation provided: "System binaries are protected by System Integrity Protection"
- Guidance: "Please use your own binaries or development builds instead"
- No suggestion to disable SIP

**Affected Paths:**
- `/System/*`
- `/bin/*`
- `/sbin/*`
- `/usr/bin/*` (except `/usr/local/bin/*`)
- Applications signed by Apple

**Reason:**
Even with administrator privileges, Developer ID certificates, all debugging entitlements, and code signing, SIP prevents debugging tools from accessing system processes.

### Developer Tools Permission
**Given:** macOS requires Developer Tools access for process attachment
**When:** Permission is not granted
**Then:**
- Clear error: "Developer Tools access required"
- How to enable: "System Settings → Privacy & Security → Developer Tools → Enable for Terminal"
- This is a fixable permission issue

### Code Signing Requirements
**Given:** macOS enforces code signing for dynamic instrumentation
**When:** Binary lacks proper signature
**Then:**
- Error indicates missing or insufficient signature
- Guidance provided for signing with appropriate certificate
- Ad-hoc signing (`codesign -s -`) works for local Terminal only
- Developer ID required for SSH/remote sessions

## Linux Limitations

### Ptrace Restrictions
**Given:** Linux uses ptrace_scope to control debugging permissions
**When:** ptrace_scope prevents attachment
**Then:**
- Check `/proc/sys/kernel/yama/ptrace_scope` value
- Values explained:
  - `0`: Classic ptrace (permissive)
  - `1`: Restricted ptrace (default on Ubuntu)
  - `2`: Admin-only attach
  - `3`: No attach
- Provide appropriate solution based on value

**Solutions:**
- For development: Consider setting ptrace_scope to 0
- For production: Use `CAP_SYS_PTRACE` capability
- Run with appropriate user permissions
- Use sudo if necessary

### Container Environments
**Given:** Docker/container environments have additional restrictions
**When:** Tracing in a container
**Then:**
- Require: `--cap-add=SYS_PTRACE` for ptrace capabilities
- Require: `--security-opt seccomp=unconfined` for full syscall access
- For cross-container tracing: host PID namespace access needed

## Windows Limitations

### Protected Processes
**Given:** Windows protects certain system processes
**When:** Attempting to attach to protected processes
**Then:**
- System-critical services cannot be traced
- Anti-malware processes are protected
- DRM-protected applications are off-limits
- Focus on user-mode applications instead

### Code Signing
**Given:** Windows code signing requirements
**When:** Deploying tracing tools
**Then:**
- Kernel-mode code must be signed with EV certificate
- User-mode typically doesn't require signing for debugging
- Administrator privileges often needed

## Universal Restrictions (All Platforms)

### Kernel-Mode Code
Cannot trace kernel-mode code without special privileges

### Platform Security Features
Cannot bypass platform security features (by design)

### Protected/Encrypted Processes
Cannot attach to protected or encrypted processes

### Security Boundaries
Cannot trace across security boundaries without elevation

## Best Practices

### For Consistent Testing
1. Always use development builds for comprehensive tracing
2. Place binaries in user directories to avoid permission issues
3. Test with appropriate privileges for your use case
4. Respect platform security models

### For Development
1. Use custom test binaries (not system utilities)
2. Place test binaries in project directories
3. Sign binaries appropriately for each platform
4. Document platform-specific test requirements

## Edge Cases

### System Binary Detection
**Given:** A path is provided for tracing
**When:** The system checks if it's a system binary
**Then:**
- Check path against known system paths
- Check bundle identifier for Apple-signed apps
- Provide immediate, clear feedback
- Do not attempt the operation

### Permission vs. Platform Limitation
**Given:** An operation fails
**When:** Determining the cause
**Then:**
- Distinguish between:
  - Fixable: Missing Developer Tools permission
  - Unfixable: System binary protected by SIP
- Provide appropriate guidance for each
- Never suggest disabling platform security

### Platform Detection Response
**Given:** A platform limitation is detected
**When:** Error response is generated
**Then:**
- JSON format:
```json
{
  "platform": "macos",
  "limitation_detected": "system_binary",
  "target": "/bin/echo",
  "message": "Cannot trace system binary on macOS",
  "suggestion": "Use custom binaries in user directories",
  "fixable": false
}
```

### Future Platform Considerations
**Given:** New platforms or OS versions are released
**When:** ADA is used on those platforms
**Then:**
- Document new limitations as discovered
- Update detection logic
- Provide platform-appropriate guidance
- Examples: iOS/iPadOS debugging, Android SELinux, WebAssembly sandboxes

## Error Message Framework

### Clarity Requirements
- Use plain language, not kernel diagnostics
- Distinguish fixable from unfixable
- Provide step-by-step remediation when possible
- Link to documentation for details
- Never suggest security-compromising workarounds

### Example: System Binary on macOS
```
ERROR: Cannot trace system binary '/bin/ls' on macOS

Reason: System binaries are protected by System Integrity Protection.
This is a platform security feature that cannot be bypassed.

Solution: Use your own binaries or development builds instead.
Place test binaries in user-controlled directories:
  - ~/bin/
  - Project directories
  - /usr/local/bin/

See: docs/specs/PLATFORM_LIMITATIONS.md (archived)
```

## References

- Original: `docs/specs/PLATFORM_LIMITATIONS.md` (archived source)
- Related: `CN-002-code-signing` (Code Signing Requirements)
- Related: `CN-003-entitlements` (Entitlements)
- Related: `EV-001-permissions-environment` (Permissions and Environment Setup)
