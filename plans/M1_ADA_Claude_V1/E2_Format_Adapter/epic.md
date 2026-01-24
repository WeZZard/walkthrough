# E2: Format Adapter Layer

## Layer

ADA Core - `ada query events` output formats

## Why First

- Only epic requiring ADA Rust code changes
- Defines the contract between ADA and Claude's reasoning
- All higher layers depend on this output format

## Interface Contract

```
ada query <bundle> events --format <FORMAT>
```

### Formats

| Format | Output | Use Case |
|--------|--------|----------|
| `text` | Tabular with headers | Human-readable default |
| `json` | `{timestamp_ns, thread_id, depth, kind, function_id, function_name}` | Programmatic access |
| `line` | `ns=... \| T=... \| thread:... \| path:... \| depth:... \| KIND func()` | Grep-friendly, LLM reasoning |

### Line Format Spec

```
ns=949066051830500 | T=0.000000s | thread:0 | path:0.0 | depth:1 | CALL main()
ns=949066051851666 | T=0.000021s | thread:0 | path:0.0.0 | depth:2 | CALL login()
ns=949066051869375 | T=0.000039s | thread:0 | path:0.0.0 | depth:2 | RETURN login()
ns=949066051874791 | T=0.000044s | thread:0 | path:0.0.1 | depth:2 | CALL logout()
```

Fields:
- `ns=` - Raw nanosecond timestamp (platform continuous clock, ground truth timecode)
- `T=` - Human-readable seconds from first event
- `thread:` - Thread ID
- `path:` - Index path in call tree (thread.sibling.child...)
- `depth:` - Call stack depth
- Event kind (CALL/RETURN/EXCEPT) + function name with parentheses

## Design Decision: Nanoseconds vs CPU Cycles

The original spec mentioned "CPU cycles" but the implementation uses **platform continuous clock (nanoseconds)**. This is the correct choice for ADA:

| Requirement | CPU Cycles | Nanoseconds |
|-------------|-----------|-------------|
| Cross-thread sorting | No - cores may have unsync'd counters | Yes - consistent across all cores |
| Voice correlation | No - can't convert to seconds reliably | Yes - direct conversion to seconds |
| Video seeking | No - variable CPU frequency | Yes - fixed timebase |
| Multi-process support | No - different cycle bases | Yes - system-wide monotonic |

**Rationale:** ADA is a *multimodal* debugging tool. Events must correlate with voice transcripts (Whisper outputs seconds) and video frames (ffmpeg uses seconds). Platform continuous clock (`mach_continuous_time` on macOS) provides a monotonic, system-wide timebase that enables this correlation.

## Location

`ada-cli/src/query/output.rs`

## Implementation

### Data Structures

- `OutputFormat::Line` - New enum variant
- `EnrichedEvent` - Event with computed path index and relative time
- `PathTracker` - Per-thread call stack tracker for computing hierarchical path indices

### Algorithm

PathTracker maintains per-thread state:
- Stack of sibling indices at each depth
- Sibling counters per depth level

For CALL events: push sibling index, reset child counter, build path
For RETURN/EXCEPT events: build path from stack, pop

### Edge Cases

- **Orphan returns**: Return without matching call uses just thread_id as path
- **Empty events**: Returns "(no events)\n"
- **Interleaved threads**: PathTracker maintains separate stack per thread_id

## Deliverables

1. [x] Add `OutputFormat::Line` variant
2. [x] Implement `format_events_line()` with nanosecond timestamps
3. [x] Implement `PathTracker` to compute hierarchical path indices
4. [x] Implement `compute_enriched_events()` for path + relative time
5. [x] Update `--format` help text to include `line`
6. [x] Unit tests for PathTracker and enrichment

## Tests

- `test_output_format__parse_line__then_line`
- `test_path_tracker__single_call__then_thread_sibling_path`
- `test_path_tracker__nested_calls__then_hierarchical_path`
- `test_path_tracker__sibling_calls__then_incremented_index`
- `test_path_tracker__multiple_threads__then_independent_paths`
- `test_path_tracker__orphan_return__then_fallback_path`
- `test_compute_enriched_events__timestamps__then_relative_seconds`
- `test_path_tracker__exception__then_same_as_return`
- `test_path_tracker__unknown_event_kind__then_thread_id_only`

## Status

Completed
