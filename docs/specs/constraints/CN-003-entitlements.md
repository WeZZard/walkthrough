---
id: CN-003
title: Entitlements and Runtime Flags
status: active
source: docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md
---

# Entitlements and Runtime Flags

## Context

**Given:**
- macOS uses entitlements to grant specific capabilities to signed binaries
- Different entitlements enable different debugging operations
- Hardened Runtime flags interact with debugging capabilities
- Target binaries and tracer binaries have different entitlement needs
- Incorrect entitlements cause injection and attachment failures

## Trigger

**When:** The ADA tracer attempts operations requiring specific entitlements

## Outcome

**Then:**
- Required entitlements are documented for each use case
- Missing entitlements are detected and reported clearly
- Users receive guidance on which entitlements to add
- Entitlement files are provided for common scenarios
- The system never modifies entitlements without explicit user consent

## Required Entitlements for Tracer Binary

### Core Debugging Entitlements

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <!-- Allow debugging/injection of this binary -->
    <key>com.apple.security.get-task-allow</key>
    <true/>

    <!-- Allow injection of unsigned/differently-signed libraries -->
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

### Entitlement Descriptions

#### com.apple.security.get-task-allow
- **Purpose:** Allows other processes to debug/inject into this binary
- **Required for:** Tracer binary when using Frida
- **Security note:** Development only; never ship with this in production

#### com.apple.security.cs.disable-library-validation
- **Purpose:** Allows loading of libraries not signed with same Team ID
- **Required for:** Loading Frida agent into traced process
- **Alternative:** Sign agent with same Team ID as target

## Required Entitlements for Target Binary (Dev Builds)

### Development Tracing Entitlements

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <!-- Allow debugging -->
    <key>com.apple.security.get-task-allow</key>
    <true/>

    <!-- Allow JIT compilation (required for Frida) -->
    <key>com.apple.security.cs.allow-jit</key>
    <true/>

    <!-- Allow library injection -->
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

### Additional Entitlement

#### com.apple.security.cs.allow-jit
- **Purpose:** Allows Just-In-Time code compilation
- **Required for:** Frida Gum-based instrumentation
- **Security note:** Development only; JIT is a security risk in production

## Hardened Runtime Flags

### Incompatible Runtime Flags
Some Hardened Runtime flags conflict with debugging:

| Flag | Effect | Solution |
|------|--------|----------|
| `runtime` | Enables Hardened Runtime | Add JIT and library validation exceptions |
| `library-validation` | Enforces library signing | Disable for dev or sign agent with same Team ID |
| `restrict` | Restricts DYLD variables | May interfere with injection |

### Checking Runtime Flags
```bash
codesign -d --entitlements - <binary>
codesign -d --verbose <binary> | grep "runtime"
```

## Edge Cases

### Missing get-task-allow
**Given:** Tracer binary lacks get-task-allow entitlement
**When:** Attempting to spawn/attach
**Then:**
- Error: "Unable to access process / permission denied"
- Solution: Re-sign with entitlements.plist including get-task-allow
- Provide exact codesign command

### Missing cs.allow-jit in Target
**Given:** Target binary does not allow JIT
**When:** Frida attempts to install hooks
**Then:**
- Error: "JIT blocked / protection faults"
- Solution for dev builds:
  - Re-sign target with cs.allow-jit entitlement
  - Provide entitlements.plist template
- Note: This is safe for development, not for production

### Library Validation Mismatch
**Given:** Target has library validation enabled, agent has different Team ID
**When:** Agent injection is attempted
**Then:**
- Error: "Code signature invalid / library validation failed"
- Option A (preferred for dev):
  - Disable library validation on target
  - Add cs.disable-library-validation entitlement
- Option B:
  - Sign agent with same Team ID as target
  - Requires paid Developer account for both

### Entitlement Inspection
**Given:** User needs to check current entitlements
**When:** Diagnosis is needed
**Then:**
- Provide command:
```bash
codesign -d --entitlements - --xml <binary>
```
- Parse output to show which entitlements are present
- Highlight missing required entitlements

### Conflicting Hardened Runtime
**Given:** Target has Hardened Runtime with restrictive flags
**When:** Injection fails
**Then:**
- Detect specific runtime flags causing conflict
- Provide targeted solution:
  - If library-validation: Add exception or disable
  - If restrict: Note potential DYLD interference
- Guidance specific to identified flag

## Signing Procedure

### For Tracer Binary
```bash
# 1. Create entitlements.plist (see template above)

# 2. Sign with Developer ID (for SSH/remote)
codesign --force --options runtime \
         --entitlements entitlements.plist \
         --sign "Developer ID Application: Your Name" \
         ./ada

# 3. Verify
codesign -d --entitlements - --xml ./ada
```

### For Target Binary (Dev Build)
```bash
# 1. Create target_entitlements.plist (with JIT, etc.)

# 2. Re-sign your app
codesign -s "Apple Development: Your Name (TEAMID)" \
         --entitlements target_entitlements.plist \
         --force \
         /path/to/YourApp.app/Contents/MacOS/YourApp

# 3. Verify
codesign -d --entitlements - --xml /path/to/YourApp.app/Contents/MacOS/YourApp
```

### For Agent Library (Optional)
```bash
# Sign agent with same Team ID as target (if avoiding library validation exceptions)
codesign -s "Apple Development: Your Name (TEAMID)" \
         libagent.dylib
```

## Entitlement Templates

### Minimal Tracer (Local Terminal Only)
```xml
<dict>
    <key>com.apple.security.get-task-allow</key><true/>
    <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
```

### Full Tracer (SSH/Remote)
Same as minimal, but must use Developer ID certificate ($99/year)

### Development Target
```xml
<dict>
    <key>com.apple.security.get-task-allow</key><true/>
    <key>com.apple.security.cs.allow-jit</key><true/>
    <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
```

## Guardrails

### Never Auto-Apply
- Never automatically add or modify entitlements
- Always show user what will be done
- Require explicit confirmation
- Provide copy-paste commands

### Security Warnings
- Clearly mark development-only entitlements
- Warn against using get-task-allow in production
- Warn against JIT in production
- Explain security implications

### Least Privilege
- Only request necessary entitlements
- Document why each entitlement is needed
- Provide alternatives where possible

## Platform-Specific Notes

### macOS Only
These entitlements are macOS-specific. Other platforms:
- **Linux:** Uses capabilities (CAP_SYS_PTRACE), not entitlements
- **Windows:** Uses privileges (SeDebugPrivilege), not entitlements

## References

- Original: `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md` (archived source)
- Related: `CN-002-code-signing` (Code Signing Requirements)
- Related: `EV-001-permissions-environment` (Permissions and Environment Setup)
