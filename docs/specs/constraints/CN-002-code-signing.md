---
id: CN-002
title: Code Signing Requirements
status: active
source: docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md
---

# Code Signing Requirements

## Context

**Given:**
- Platform security requires signed code for dynamic instrumentation
- Different environments (local, SSH, CI/CD) have different signing requirements
- Certificate types have varying costs and capabilities
- Users need clear guidance on signing requirements and procedures

## Trigger

**When:** The ADA tracer attempts to attach to or spawn a process requiring code signing

## Outcome

**Then:**
- The system detects the current signature type and environment
- Appropriate error messages are displayed when signing is insufficient
- Solutions are provided based on certificate availability and use case
- Users can sign binaries correctly for their target environment
- The system never auto-signs (requires explicit user confirmation)

## macOS Code Signing

### Certificate Types and Capabilities

| Certificate Type | Local Terminal | Cost | SSH Session | CI/CD | Distribution |
|-----------------|----------------|------|-------------|--------|--------------|
| None | ❌ | Free | ❌ | ❌ | ❌ |
| Ad-hoc (`-`) | ✅ | Free | ❌ | ❌ | ❌ |
| Apple Development | ✅ | Free* | ⚠️ | ❌ | ❌ |
| Apple Developer ID | ✅ | $99/year | ✅ | ✅ | ✅ |

*Free with Apple ID via Xcode

### Required Entitlements

The tracer binary must be signed with these entitlements:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.get-task-allow</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

### Signing Commands

#### Developer ID (Paid Certificate)
```bash
codesign --force --options runtime \
         --entitlements entitlements.plist \
         --sign "Developer ID Application: Your Name" \
         <binary>
```

#### Ad-hoc Signing (Local Only)
```bash
codesign --force --entitlements entitlements.plist --sign - <binary>
```

## Edge Cases

### SSH Session Detection
**Given:** The tracer is running over an SSH connection
**When:** Attempting to trace with ad-hoc signature
**Then:**
- Detect SSH session: `[[ -n "$SSH_CLIENT" ]]`
- Display error:
```
ERROR: Cannot trace binary over SSH connection

Current signature: ad-hoc
Required: Apple Developer ID certificate

Solutions:
1. Sign with Developer ID ($99/year):
   ./utils/sign_binary.sh <binary>

2. Use local Terminal.app instead of SSH

3. Deploy daemon mode (future release)

See: CN-002-code-signing (this document), CN-003-entitlements
```

### Signature Type Detection
**Given:** A signing failure occurs
**When:** The system diagnoses the issue
**Then:**
- Detect current signature:
```bash
codesign -dv <binary> 2>&1 | grep "Signature"
```
- Parse signature type (none, ad-hoc, Development, Developer ID)
- Provide appropriate solution based on type and environment

### Missing Entitlements
**Given:** Binary is signed but missing required entitlements
**When:** Injection fails
**Then:**
- Detect missing entitlements via error code
- Display which entitlements are needed
- Provide command to re-sign with correct entitlements
- Require explicit user confirmation before re-signing

### Library Validation Conflict
**Given:** Target binary has library validation enabled
**When:** Agent injection fails
**Then:**
- Error: "Code signature invalid / library validation failed"
- Solution A (preferred for dev builds):
  - Disable Library Validation on target
  - Add `cs.disable-library-validation` entitlement
- Solution B:
  - Sign agent with same Team ID as target
- Never auto-modify without user consent

### JIT Blocked
**Given:** Target needs JIT compilation for Frida hooks
**When:** JIT is not allowed
**Then:**
- Error: "JIT blocked / protection faults"
- Solution: Add `cs.allow-jit` to target (dev builds only)
- Provide signing command with JIT entitlement

### Hardened Runtime Interaction
**Given:** Target has Hardened Runtime enabled
**When:** Injection is attempted
**Then:**
- Check for runtime flags
- Provide specific guidance for each flag
- May require disabling specific hardened features for tracing
- Document which features can be safely disabled for dev builds

## macOS-Specific Requirements

### Developer Mode (macOS 13+)
**Given:** macOS 13 or later
**When:** Tracing is attempted
**Then:**
- Developer mode must be enabled
- Check: Settings → Privacy & Security → Developer Mode
- Required even for signed binaries
- One-time setup per machine

### System Integrity Protection (SIP)
**Given:** SIP is enabled (default)
**When:** Attempting to trace system binaries
**Then:**
- Cannot trace system binaries (see CN-001)
- This is a platform limitation, not a signing issue
- Do not suggest disabling SIP

## Linux Code Signing

Linux generally does not require code signing for debugging, but uses capability-based security:

### Capabilities vs. Signing
- Linux uses `CAP_SYS_PTRACE` instead of code signing
- See EV-001 for permission setup
- Some distributions may have additional requirements

## Windows Code Signing

### Authenticode
**Given:** Distributing Windows binaries
**When:** Signing is required
**Then:**
- Use Authenticode certificate
- Kernel-mode drivers require EV certificate
- User-mode tools: test signing for development

### Debug Privileges
**Given:** Windows debugging operations
**When:** Process attachment is needed
**Then:**
- Require `SeDebugPrivilege`
- Administrator rights typically required
- Not strictly a signing issue

## Error Detection Framework

```rust
enum SignatureError {
    MacOS {
        current_type: SignatureType,
        required_type: SignatureType,
        is_ssh: bool,
        missing_entitlements: Vec<String>,
    },
    Linux {
        // Capabilities, not signing
    },
    Windows {
        required_privilege: String,
    },
}

impl SignatureError {
    fn user_message(&self) -> String {
        // Platform-specific guidance
    }

    fn remediation_steps(&self) -> Vec<String> {
        // Ordered list of solutions
    }
}
```

## Testing Matrix

### macOS
- [ ] Local Terminal.app with ad-hoc signing
- [ ] Local Terminal.app with Developer ID
- [ ] SSH session with ad-hoc signing (should fail gracefully)
- [ ] SSH session with Developer ID (should succeed)
- [ ] CI/CD with ad-hoc + entitlements
- [ ] Missing entitlements detection
- [ ] Library validation conflict detection

### Linux
- [ ] No signing required (verify)
- [ ] Capability-based checks work
- [ ] Clear distinction from macOS requirements

### Windows
- [ ] Test signing for development
- [ ] Privilege checks (not signature checks)

## Guardrails

### Never Auto-Sign
- Never automatically re-sign binaries
- Always require explicit user confirmation
- Provide commands for user to execute
- Explain what the signature does

### Never Suggest Security Bypass
- Do not suggest disabling SIP
- Do not suggest disabling Gatekeeper
- Do not suggest blanket security reductions
- Provide minimal necessary changes only

### Clear Documentation
- Link to detailed signing procedures
- Explain certificate requirements
- Document cost implications (Developer ID)
- Provide examples for each use case

## References

- Original: `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md` (archived source)
- Related: `CN-003-entitlements` (Entitlements Detail)
- Related: `EV-001-permissions-environment` (Permissions and Environment)
- Related: `CN-001-platform-limitations` (Platform Limitations)
