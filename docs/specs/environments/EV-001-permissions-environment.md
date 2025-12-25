---
id: EV-001
title: Permissions and Environment Setup
status: active
source: docs/specs/PERMISSIONS_AND_ENVIRONMENT.md
---

# Permissions and Environment Setup

## Context

**Given:**
- Tracing requires specific system permissions and configuration
- Different environments (local, SSH, CI/CD) have different requirements
- Users need deterministic, fast preflight diagnostics
- Remediation steps must follow least-privilege principles
- Human approval is required for any code-signing or entitlement changes

## Trigger

**When:** The user initiates tracing or runs preflight diagnostics

## Outcome

**Then:**
- A preflight diagnostics check runs before tracing attempts
- Clear pass/fail report with specific reasons is provided
- Remediation steps are ordered and actionable
- The system distinguishes fixable from unfixable constraints
- No SIP relaxations or security-compromising workarounds are suggested
- Human approval is required for any binary modifications

## Preflight Diagnostics

### Input
- Target path or PID

### Output
JSON report consumed by AI agent and CLI:
```json
{
  "devtools_ok": false,
  "sudo_recommended": true,
  "target": {
    "path": "/path/app",
    "is_system_binary": false,
    "sip_protected": false,
    "signed": true,
    "team_id": "ABCDE12345",
    "hardened_runtime": true,
    "library_validation": true,
    "entitlements": {
      "get_task_allow": false,
      "cs_allow_jit": false,
      "cs_disable_library_validation": false
    }
  },
  "agent": {
    "path": "libagent.dylib",
    "signed": false,
    "team_id": null
  },
  "constraints": {
    "protected_process": false,
    "jit_required": true
  },
  "remediation": [
    {
      "id": "enable_devtools",
      "summary": "Enable Developer Tools permission for Terminal/iTerm",
      "steps": [
        "Open System Settings → Privacy & Security → Developer Tools",
        "Enable for your terminal app"
      ]
    },
    {
      "id": "run_with_sudo",
      "summary": "Run tracer with sudo",
      "steps": ["sudo ada start spawn ..."]
    }
  ]
}
```

### Checks Performed

#### System Binary Detection
- Is target in SIP-protected paths (/System, /bin, /sbin, /usr/bin)?
- Check bundle identifiers for Apple-signed apps
- Immediate fail with clear guidance (see CN-001)

#### Developer Tools Permission (TCC)
- Attempt attach to a spawned helper owned by same user
- Check if Developer Tools permission is granted
- macOS-specific check

#### Sudo Requirement
- Attempt attach with/without sudo
- Classify based on error codes
- Determine if sudo is necessary

#### Target Signing Flags
- Hardened Runtime enabled?
- Library Validation enabled?
- Team ID present?

#### Target Entitlements
- get-task-allow present?
- cs.allow-jit present?
- cs.disable-library-validation present?

#### Agent Signing
- Team ID matches target?
- Signature present?

#### JIT Capability
- Required for Gum-based hooks
- Check if allowed by target

#### Protected/SIP Process
- Detected via path check
- Detected via bundle identifiers
- Result: unsupported (see CN-001)

## Error-to-Action Mapping

### Unable to Access Process / Permission Denied
**If** `devtools_ok` is false:
- **Action:** Enable Developer Tools
- **Steps:** System Settings → Privacy & Security → Developer Tools → Enable for terminal app

**Else:**
- **Action:** Run with sudo
- **Steps:** `sudo -E ada ...` (preserve environment)

### Code Signature Invalid / Library Validation Failed
**Option A (preferred for dev builds):**
- Disable Library Validation on target
- Add `cs.disable-library-validation` entitlement

**Option B:**
- Sign agent with same Team ID as target

### JIT Blocked / Protection Faults
- **Action:** Add `cs.allow-jit` to target (dev builds only)
- **Steps:** Re-sign with JIT entitlement

### System Binary Detected
- **Error:** "Cannot trace system binaries on macOS - they are protected by System Integrity Protection"
- **Suggestion:** "Please use your own binaries or development builds instead"
- **DO NOT:** Suggest disabling SIP or other security-compromising workarounds

### Protected/SIP Process
- Declare unsupported
- Suggest tracing a dev build instead

## Remediation Procedures (macOS)

### Developer Tools Permission
**UI Path:**
1. Open System Settings
2. Navigate to Privacy & Security
3. Select Developer Tools
4. Enable for Terminal/iTerm (or your terminal app)

**Result:** Grants debugging permissions to the terminal app

### Sudo Usage
Re-run commands with sudo:
```bash
sudo -E ada trace /path/to/binary
```

The `-E` flag preserves environment variables if needed.

### Remote Sessions (SSH)
**Problem:** GUI Developer Tools permission for Terminal.app does not apply to SSH shells (launched by `sshd`)

**Solution:**
```bash
# Enable remote developer access
sudo /usr/sbin/DevToolsSecurity -enable

# Add your user to the developer group
sudo dseditgroup -o edit -t user -a "$USER" _developer
```

**Note:** Even with the above, some targets still require `sudo` for attach/injection due to Hardened Runtime or Library Validation constraints.

**Optional Lab Setup:**
- Run `frida-server` as root on the target
- Connect remotely (`frida -H host:port`)
- Avoids TCC prompts but increases privilege and operational risk
- Use only in controlled environments

### Code Signing / Entitlements (Dev Builds)

**Generate entitlements.plist:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.get-task-allow</key><true/>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
</plist>
```

**Re-sign target binary:**
```bash
codesign -s "Apple Development: Your Name (TEAMID)" \
  --entitlements entitlements.plist --force \
  /path/to/YourApp.app/Contents/MacOS/YourApp
```

**Sign agent (optional):**
```bash
codesign -s "Apple Development: Your Name (TEAMID)" libagent.dylib
```

## Runtime Controls (Startup Timeout)

The tracer enforces a unified startup timeout derived from estimated hook installation workload plus tolerance.

### Environment Overrides (Expert)
- `ADA_STARTUP_TIMEOUT` — hard override of computed timeout (milliseconds)
- `ADA_STARTUP_WARM_UP_DURATION` — warm-up duration in estimation (milliseconds)
- `ADA_STARTUP_PER_SYMBOL_COST` — per-symbol cost in estimation (milliseconds)
- `ADA_STARTUP_TIMEOUT_TOLERANCE` — fractional tolerance (e.g., 0.15 for 15%)

### CLI Override
```bash
ada trace --startup-timeout <ms> /path/to/binary
```

**Use overrides sparingly.** The default computed timeout is designed to install hooks completely, then observe behavior.

## API

### REST/RPC Endpoint
```
GET /diagnose?path=<path>&pid=<pid>
```
Returns the JSON report (see above).

### CLI
```bash
# Human summary
ada diagnose <path|pid>

# JSON output
ada diagnose <path|pid> --json
```

## Edge Cases

### Multiple Issues Simultaneously
**Given:** Target has multiple problems (unsigned, missing JIT, no dev tools permission)
**When:** Diagnostics run
**Then:**
- Report ALL issues in remediation list
- Order by ease of fix (permissions first, signing last)
- User can address incrementally
- Re-run diagnostics after each step

### Partial Remediation
**Given:** User fixes one issue but others remain
**When:** Tracing is attempted again
**Then:**
- New error reflects remaining issues
- Diagnostics clearly show what's left to fix
- Progress is visible

### Conflicting Requirements
**Given:** SSH session requires Developer ID, but user only has ad-hoc
**When:** Diagnostics run
**Then:**
- Clearly state: "Ad-hoc signing not sufficient for SSH"
- Provide alternatives:
  1. Obtain Developer ID ($99/year)
  2. Use local Terminal instead of SSH
  3. Future: daemon mode
- Do not proceed with insufficient signature

### System Binary False Positive
**Given:** A binary in /usr/local/bin (user-controlled)
**When:** System binary check runs
**Then:**
- /usr/local/bin is NOT considered a system path
- Check passes
- Tracing proceeds normally

## Guardrails

### No SIP Relaxation
- Never suggest disabling or weakening SIP
- Never suggest dtrace entitlements as a workaround for Frida limitations

### No Auto Re-Signing
- Never automatically re-sign binaries
- Generate commands and ask for explicit user consent
- Explain what each signing step does

### Concise Messages
- Keep error messages high-level
- Link to this spec for technical background
- Provide copy-paste commands where possible

## Cross-Platform Notes

### Linux
- **Permissions:** `CAP_SYS_PTRACE` or `ptrace_scope` settings
- **MAC:** SELinux/AppArmor contexts
- **eBPF:** Specific kernel capabilities required
- No code signing requirements (different security model)

### Windows
- **Privileges:** `SeDebugPrivilege` (Debug privilege)
- **Code Integrity:** Constraints on kernel-mode code
- **Alternatives:** ETW (Event Tracing for Windows) for some scenarios

## References

- Original: `docs/specs/PERMISSIONS_AND_ENVIRONMENT.md` (archived source)
- Related: `CN-002-code-signing` (Code Signing Requirements)
- Related: `CN-003-entitlements` (Entitlements Detail)
- Related: `CN-001-platform-limitations` (Platform Limitations)
