---
id: BH-002
title: ATF Index File Format
status: active
source: docs/specs/TRACE_SCHEMA.md
---

# ATF Index File Format

## Context

**Given:**
- The system needs to capture ALL function call/return events at maximum throughput
- Events must be stored in a format optimized for streaming performance
- Per-thread organization is used to maximize cache locality
- Fixed-size records enable O(1) random access
- The format must support bidirectional linking to detail events

## Trigger

**When:** The tracer writes index events to persistent storage during a tracing session

## Outcome

**Then:**
- Each thread writes to its own `index.atf` file in `thread_N/index.atf`
- The file contains a 64-byte header, followed by fixed 32-byte index events, followed by a 64-byte footer
- All events are captured at full throughput with no holes
- Events can be randomly accessed by position (O(1) lookup)
- Events can link forward to detail events via `detail_seq` field
- The file supports append-only writes during recording
- The format enables cross-thread merge-sort by timestamp for causality analysis

## File Structure

### File Layout
```
[Header - 64 bytes]     <- Placeholder first, updated at finalize
[Index Events]          <- Fixed 32-byte records, ALL events, compact
[Footer - 64 bytes]     <- For crash recovery
```

### Index Header (64 bytes)

```c
typedef struct __attribute__((packed)) {
    // Identity (16 bytes)
    uint8_t  magic[4];           // "ATI2" (ATF Index v2)
    uint8_t  endian;             // 0x01 = little-endian
    uint8_t  version;            // 1
    uint8_t  arch;               // 1=x86_64, 2=arm64
    uint8_t  os;                 // 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows
    uint32_t flags;              // Bit 0: has_detail_file
    uint32_t thread_id;          // Thread ID for this file

    // Timing metadata (8 bytes)
    uint8_t  clock_type;         // 1=mach_continuous, 2=qpc, 3=boottime
    uint8_t  _reserved1[3];
    uint32_t _reserved2;

    // Event layout (8 bytes)
    uint32_t event_size;         // 32 bytes per event
    uint32_t event_count;        // Total number of events

    // Offsets (16 bytes)
    uint64_t events_offset;      // Offset to first event
    uint64_t footer_offset;      // Offset to footer (for recovery)

    // Time range (16 bytes)
    uint64_t time_start_ns;      // First event timestamp
    uint64_t time_end_ns;        // Last event timestamp
} AtfIndexHeader;
```

### Index Event (32 bytes)

```c
typedef struct __attribute__((packed)) {
    uint64_t timestamp_ns;       // ada_get_timestamp_ns() - THE genlock
    uint64_t function_id;        // (moduleId << 32) | symbolIndex
    uint32_t thread_id;          // Thread identifier
    uint32_t event_kind;         // CALL=1, RETURN=2, EXCEPTION=3
    uint32_t call_depth;         // Call stack depth
    uint32_t detail_seq;         // Forward link to detail event (UINT32_MAX = none)
} IndexEvent;
```

**Field Definitions:**

| Field | Size | Description |
|-------|------|-------------|
| `timestamp_ns` | 8 | Nanoseconds from platform continuous clock (genlock) |
| `function_id` | 8 | (moduleId << 32) \| symbolIndex |
| `thread_id` | 4 | OS thread identifier |
| `event_kind` | 4 | 1=CALL, 2=RETURN, 3=EXCEPTION |
| `call_depth` | 4 | Current call stack depth |
| `detail_seq` | 4 | Forward link to detail event, UINT32_MAX if none |

**Note:** `index_seq` is implicit = position in file (event_offset / 32)

### Index Footer (64 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           // "2ITA" (reversed)
    uint32_t checksum;           // CRC32 of events section
    uint64_t event_count;        // Actual event count (authoritative)
    uint64_t time_start_ns;      // First event timestamp
    uint64_t time_end_ns;        // Last event timestamp
    uint64_t bytes_written;      // Total bytes in events section
    uint8_t  reserved[28];
} AtfIndexFooter;
```

## Session Directory Layout

```
ada_traces/session_YYYYMMDD_HHMMSS/pid_XXXXX/
├── manifest.json           <- Session metadata, lists all thread files
├── thread_0/
│   ├── index.atf           <- Thread 0 index events
│   └── detail.atf          <- Thread 0 detail events (if recorded)
├── thread_1/
│   ├── index.atf           <- Thread 1 index events
│   └── detail.atf          <- Thread 1 detail events (if recorded)
└── ...
```

## Timing and Synchronization

### Platform Timing API (Genlock)

All timestamps use platform-specific continuous clocks:
- macOS/iOS: `mach_continuous_time()` (~20-40ns overhead)
- Windows: `QueryPerformanceCounter()` (~20-50ns overhead)
- Linux/Android: `clock_gettime(CLOCK_BOOTTIME)` (~50-100ns overhead)

Properties:
- Cross-core synchronized (guaranteed by OS)
- Nanosecond precision
- Continuous through sleep (critical for mobile sessions)

### Cross-Thread Ordering

The `timestamp_ns` field IS the genlock because:
- Platform APIs provide total ordering across all cores
- No need for separate CPU counter
- Cross-thread merge-sort by `timestamp_ns` gives true causality

## Edge Cases

### Bidirectional Navigation
**Given:** An index event with a detail_seq field
**When:** The reader needs to access the corresponding detail event
**Then:**
- Forward navigation: detail = detail_reader.get(index_event.detail_seq)
- Backward navigation: index = index_reader.get(detail_event.index_seq)
- Both directions are O(1) lookups, not scans

### Cross-Thread Analysis
**Given:** Multiple per-thread index files
**When:** A cross-thread causality analysis is needed
**Then:**
- Load all thread index files
- Merge-sort by timestamp_ns
- Result gives true cross-thread causality ordering

### Media Stream Correlation
**Given:** Audio/screen recording needs to be synchronized with trace events
**When:** Correlation is required
**Then:**
- Capture TimingAnchor with both hostTimeNs (ada_get_timestamp_ns()) and mediaTime (CMTime)
- Use anchor to align media timeline with trace timeline

## Design Rationale

### Per-Thread Files (Not Interleaved)
- Cache locality: Thread analysis doesn't pollute cache with other threads
- Parallel processing: Thread files can be processed independently
- Simpler recovery: One corrupt file doesn't affect others
- No filtering: Per-thread queries don't scan irrelevant events
- Matches architecture: Per-thread SPSC ring buffers → per-thread files

### Raw Binary (Not Protobuf)
- Zero encoding overhead: Direct memcpy from ring buffer to disk
- Streaming friendly: Append-only writes, no framing complexity
- Fixed-size index events: O(1) random access by sequence number
- Memory-mappable: Direct pointer access without deserialization
- 10M+ events/sec: Target throughput not achievable with protobuf

## References

- Original: `docs/specs/TRACE_SCHEMA.md` (archived source)
- Related: `BH-001-system-architecture` (System Architecture)
- Related: `BH-003-atf-detail-format` (ATF Detail Format)
