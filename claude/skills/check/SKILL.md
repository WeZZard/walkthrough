---
name: check
description: "Build, run, capture, and analyze any app in one step."
---

# ReadyCheck: Build, Run, Capture, and Analyze

## Purpose

Build and launch an application with ADA tracing enabled, capture execution traces, voice narration, and screen recording in the background, then automatically analyze the session when complete.

## MANDATORY: Environment Setup

Before running any ada command, set the environment:

<example>
export ADA_AGENT_RPATH_SEARCH_PATHS="${ADA_LIB_DIR}"
</example>

**IMPORTANT**: Always use the full path `${ADA_BIN_DIR}/ada` for commands to avoid conflicts with other `ada` binaries in PATH.

## Workflow

### MANDATORY: Step 1. Preflight Check

**If $PREFLIGHT_CHECK is set to 1, skip to Step 2.**

Run the ADA doctor to verify all dependencies:

<example>
${ADA_BIN_DIR}/ada doctor check --format json
</example>

Parse the JSON output. Check all fields are `ok: true`.

**If any check fails:**
1. Show the user which checks failed with fix instructions
2. Stop and ask user to fix issues
3. After fixes, re-run `ada doctor check`

**If all checks pass:**
- Set `$PREFLIGHT_CHECK = 1`
- Continue to Step 2

### MANDATORY: Step 2. Project Detection

You ***MUST*** explore the project to find the app to run and the build system building it.

### MANDATORY: Step 3. Build (if applicable)

You MAY use the app's build system to build the app.

### MANDATORY: Step 4. Start Capture (Background)

Start capturing with `run_in_background: true`:

<example>
Bash(
  command: "${ADA_BIN_DIR}/ada capture start <binary_path>",
  run_in_background: true
)
</example>

Save the returned task ID as **$CAPTURE_TASK_ID**.

**Report to user:**

> **Capture running**
>
> Interact with your app. When you quit the app, capture stops automatically.
>

### MANDATORY: Step 5. Wait for Capture Completion

Wait for the user to finish interacting with their app. When the user indicates they are done, collect the capture output:

<example>
TaskOutput(
  task_id: $CAPTURE_TASK_ID,
  block: false
)
</example>

Parse the output for the session directory path.

**If capture is still running:**
- Inform the user the app is still running
- Wait for user to quit the app, then check again

**If capture succeeded:**
- Report to user:
  > **Capture completed**
  >
  > Session directory path: [session_directory_path]
  >
- Continue to Step 6

**If capture failed:**
- Show the error message to the user
- Stop

### MANDATORY: Step 6. Auto-Analyze

Automatically invoke the analyze skill:

<example>
Skill(skill: "analyze")
</example>

Follow the analyze skill workflow from there.

## Error Handling

- **Build failure**: Show build errors, suggest fixes
- **Binary not found**: Guide user to specify path manually
- **Capture failure**: Show error output, suggest re-running
