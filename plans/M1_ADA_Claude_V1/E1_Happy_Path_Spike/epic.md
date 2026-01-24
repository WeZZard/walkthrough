# E1: Happy Path Spike

## Purpose

Run the V1 happy path manually, end-to-end. Document what works, what breaks, what's missing. Let reality inform E2-E5.

## Approach

No code changes. Just run commands and observe.

## The Spike

### Step 1: Capture

```bash
# Build ADA if needed
cd /Users/wezzard/Projects/ADA-codex
cargo build --release

# Pick a test app (any macOS app with symbols)
TEST_APP="/path/to/some/app"

# Run capture (~30 seconds, speak observations, then Ctrl+C)
./target/release/ada capture start "$TEST_APP" --output /tmp/spike_test
```

**Observe**:
- [ ] Does capture start without error?
- [ ] Does Ctrl+C stop gracefully?
- [ ] What's in the output directory?

```bash
ls -la /tmp/spike_test/
ls -la /tmp/spike_test/*.adabundle/
```

---

### Step 2: Check Bundle Structure

```bash
# Bundle manifest
cat /tmp/spike_test/*.adabundle/manifest.json | jq .

# ATF manifest (symbols)
cat /tmp/spike_test/*.adabundle/trace/*/pid_*/manifest.json | jq .
```

**Observe**:
- [ ] Does bundle manifest have expected fields?
- [ ] Does ATF manifest have symbols with readable names?
- [ ] What's the actual directory structure?

---

### Step 3: Check Multimedia

```bash
# Screen recording
ffprobe /tmp/spike_test/*.adabundle/screen.mp4

# Voice recording
ffprobe /tmp/spike_test/*.adabundle/voice.wav
```

**Observe**:
- [ ] Are files playable?
- [ ] What's the duration?
- [ ] Any encoding issues?

---

### Step 4: Query Events

```bash
# Summary
./target/release/ada query /tmp/spike_test/*.adabundle summary

# Events (text)
./target/release/ada query /tmp/spike_test/*.adabundle events limit:20

# Events (JSON)
./target/release/ada query /tmp/spike_test/*.adabundle events limit:20 --format json | jq .
```

**Observe**:
- [ ] Are function names resolved (not hex IDs)?
- [ ] What timestamp format? Nanoseconds? Cycles?
- [ ] Is the JSON structure usable?
- [ ] What fields are present?

---

### Step 5: Run Whisper

```bash
# Check if Whisper is installed
which whisper || pip install openai-whisper

# Transcribe voice
whisper /tmp/spike_test/*.adabundle/voice.wav --model base --output_format json --output_dir /tmp/spike_whisper/

# Check output
cat /tmp/spike_whisper/*.json | jq .
```

**Observe**:
- [ ] Does Whisper run?
- [ ] What's the output format?
- [ ] Are timestamps present?
- [ ] Is accuracy usable?

---

### Step 6: Extract Screenshot

```bash
# Pick a timestamp from the voice transcript (e.g., 10 seconds)
ffmpeg -ss 10 -i /tmp/spike_test/*.adabundle/screen.mp4 -frames:v 1 /tmp/spike_screenshot.png

# View it
open /tmp/spike_screenshot.png
```

**Observe**:
- [ ] Does extraction work?
- [ ] Is the image usable?

---

### Step 7: Feed to Claude (manually)

Open Claude (web or API) and provide:
1. The voice transcript text
2. The screenshot image
3. A sample of events JSON
4. Ask: "What was the code doing when the user reported the issue?"

**Observe**:
- [ ] Can Claude correlate the data?
- [ ] What format does Claude prefer?
- [ ] What's missing for Claude to reason well?
- [ ] What's the actual diagnosis quality?

---

## Spike Results (2026-01-24)

### What Worked

1. **Capture** (`ada capture start`)
   - Creates `.adabundle` directory correctly
   - Records screen.mp4 (H.264, 4096x2304)
   - Records voice.wav (48kHz mono)
   - Captures trace events across 6 threads
   - Graceful shutdown on Ctrl+C
   - Bundle manifest written correctly

2. **Bundle Structure**
   ```
   session_*.adabundle/
   ├── manifest.json       ✅ Valid JSON with expected fields
   ├── screen.mp4          ✅ Playable (7.7s, 2.9 Mbps)
   ├── voice.wav           ✅ Playable (6.5s, 48kHz)
   ├── voice.m4a           ✅ Compressed version
   └── trace/
       └── session_*/pid_*/
           ├── manifest.json  ✅ Has symbols with readable names
           └── thread_*/      ✅ ATF files present
   ```

3. **Query** (`ada query`)
   - Summary works: shows threads, symbols, event counts
   - Events work: shows timestamp, thread, depth, kind, function name
   - JSON format works (with correct flag position)
   - Function names are resolved (not hex IDs)

4. **Screenshot Extraction**
   - `ffmpeg -ss <time> -i screen.mp4 -frames:v 1 out.png` works
   - High-res output (4096x2304)

### What Broke / Issues Found

1. **Environment Variable Required**
   - Must set `ADA_AGENT_RPATH_SEARCH_PATHS=./target/release/tracer_backend/lib`
   - Without this: "Agent library not found" error
   - **Fix needed**: Auto-detect or document requirement

2. **Query Doesn't Accept Bundle Path**
   - `ada query /path/to.adabundle summary` fails
   - Must use direct trace path: `ada query /path/to.adabundle/trace/session_*/pid_* summary`
   - **Fix needed (E2)**: Parse bundle manifest to find trace_session

3. **Query Flag Order Matters**
   - `ada query <path> events --format json` → outputs TEXT
   - `ada query -f json <path> events` → outputs JSON ✅
   - **Fix needed**: Accept flags in any position

4. **Timestamp Format**
   - Events use nanoseconds (large numbers like 949066051830500)
   - Not CPU cycles as originally planned
   - Need to document/verify alignment with voice/screen

### What's Missing

1. **Whisper CLI** - Not installed (external dependency)
2. **Claude Integration Test** - Not completed (pending Whisper)
3. **Time Correlation** - Need to verify timestamp alignment across modalities

### Insights for E2-E5

**E2 (Format Adapter):**
- Current JSON format is usable but verbose
- Function names already resolved - good
- `line-complete` format still desirable for grep-ability
- **Priority**: Fix bundle path resolution first, then add formats

**E3 (Session Management):**
- Bundle manifest already contains `trace_session` path
- Skills can parse this to find trace data
- `active_session.json` may not be needed if we just pass bundle path
- **Simplification opportunity**: Just store bundle path

**E4 (Analysis Pipeline):**
- ffmpeg screenshot extraction works out of the box
- Whisper needs installation (`pip install openai-whisper`)
- Time filtering can be done post-query (filter JSON by timestamp_ns)
- **Simplification**: May not need time-range in ada query

**E5 (Skills):**
- Need to set `ADA_AGENT_RPATH_SEARCH_PATHS` in skill
- Need to handle bundle path → trace path resolution
- JSON output works for LLM consumption

---

## Blockers for Next Steps

1. ~~**Fix query bundle path resolution**~~ - ✅ FIXED (2026-01-24)
2. **Install Whisper** - Required for voice transcription
3. **Test Claude integration** - Manual test with captured data

## Status

**Complete** - Spike complete, query fixes verified

---

## Spike Rerun Results (2026-01-24, Post-Fix)

### Fixes Verified

Both blockers from the original spike have been resolved:

| Issue | Before | After |
|-------|--------|-------|
| **Bundle path resolution** | Had to use `trace/session_*/pid_*` path | Direct bundle path works ✅ |
| **Flag order matters** | `-f json` had to come before path | Flags work in any position ✅ |

### Implementation Details

Commit `a78b557`: `refactor(ada-cli): redesign query with subcommands and bundle-aware architecture`

Changes:
- Replaced variadic argument parsing with clap subcommands
- Added `Bundle` struct to parse `.adabundle/manifest.json`
- Session now receives trace path from bundle manifest
- Removed legacy `parser.rs` and `find_trace_path()`

### Verified Commands (New Syntax)

```bash
# Summary - WORKS
ada query /tmp/spike_test/*.adabundle summary

# Events with flags at end - WORKS
ada query /tmp/spike_test/*.adabundle events --limit 10 --format json

# Functions - WORKS
ada query /tmp/spike_test/*.adabundle functions

# Threads - WORKS
ada query /tmp/spike_test/*.adabundle threads

# Calls - WORKS
ada query /tmp/spike_test/*.adabundle calls main --limit 5
```

### Sample Output

```
Session: pid_12994
Module:  test_runloop (5FA20B0E-9966-39DB-BDC7-C1B31320A74F)
Threads: 6
Symbols: 155
Events:     591
```

### Remaining Work

1. **Install Whisper** - External dependency (`pip install openai-whisper`)
2. **Claude integration test** - Feed transcript + screenshot + events to Claude
