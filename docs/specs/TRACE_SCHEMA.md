# ADA Trace Format V2 (ATF v2)

This document defines the raw binary schema for the ADA Trace Format. This format is the data contract between the Tracer Backend (C/C++) and the Query Engine (Rust/Python).

**Version History:**
- V4 (superseded): Protobuf-based format - replaced due to encoding overhead
- V2 (current): Raw binary format - optimized for streaming throughput

## Design Principles

1. **Two separate files** - Index and detail lanes write to different files
2. **Index lane priority** - Index file captures ALL events at full throughput
3. **Detail lane is additive** - Detail file only created when detail recording is active
4. **No holes** - Detail file is compact, contains only captured detail events
5. **Bidirectional linking** - Index events link forward to detail, detail events link back to index
6. **O(1) navigation** - Both directions are direct lookups, not scans
7. **Append-only writes** - Both files are sequential append during recording
8. **Per-thread files** - Each thread writes to its own files (not interleaved)

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
├── thread_2/
│   └── index.atf           <- Thread 2 index events (no detail)
└── ...
```

**Benefits of per-thread files:**
1. Perfect cache locality for per-thread analysis
2. Better compression (similar events grouped)
3. Independent recovery (one corrupt file doesn't affect others)
4. Simpler per-thread queries (no filtering)
5. Matches per-thread ring buffer architecture
6. Parallel processing of thread files

---

## File 1: Index File (index.atf)

**Purpose**: Capture ALL function call/return events at maximum throughput

**File Layout:**
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
_Static_assert(sizeof(AtfIndexHeader) == 64, "Index header must be 64 bytes");
```

**Field Definitions:**

| Field | Size | Description |
|-------|------|-------------|
| `magic` | 4 | Magic bytes "ATI2" |
| `endian` | 1 | 0x01 = little-endian (canonical) |
| `version` | 1 | Format version (currently 1) |
| `arch` | 1 | CPU architecture: 1=x86_64, 2=arm64 |
| `os` | 1 | Operating system: 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows |
| `flags` | 4 | Bit 0: has_detail_file |
| `thread_id` | 4 | Thread ID for this file |
| `clock_type` | 1 | Clock source: 1=mach_continuous, 2=qpc, 3=boottime |
| `event_size` | 4 | Size of each event (32 bytes) |
| `event_count` | 4 | Total number of events |
| `events_offset` | 8 | Byte offset to first event |
| `footer_offset` | 8 | Byte offset to footer |
| `time_start_ns` | 8 | First event timestamp (nanoseconds) |
| `time_end_ns` | 8 | Last event timestamp (nanoseconds) |

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
_Static_assert(sizeof(IndexEvent) == 32, "IndexEvent must be 32 bytes");
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
_Static_assert(sizeof(AtfIndexFooter) == 64, "Index footer must be 64 bytes");
```

---

## File 2: Detail File (detail.atf) - Optional

**Purpose**: Capture rich context (registers, stack) when detail recording is active

**Created only when**: Detail recording is activated (user presses record, trigger fires, etc.)

**File Layout:**
```
[Header - 64 bytes]     <- References index file
[Detail Events]         <- Length-prefixed, compact, no holes
[Footer - 64 bytes]     <- For crash recovery
```

### Detail Header (64 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           // "ATD2" (ATF Detail v2)
    uint8_t  endian;             // 0x01 = little-endian
    uint8_t  version;            // 1
    uint8_t  arch;               // 1=x86_64, 2=arm64
    uint8_t  os;                 // 1=iOS, 2=Android, 3=macOS, 4=Linux
    uint32_t flags;              // Reserved
    uint32_t thread_id;          // Thread ID for this file
    uint32_t _reserved1;
    uint64_t events_offset;      // Offset to first event (typically 64)
    uint64_t event_count;        // Number of detail events
    uint64_t bytes_length;       // Total bytes in events section
    uint64_t index_seq_start;    // First index sequence number covered
    uint64_t index_seq_end;      // Last index sequence number covered
} AtfDetailHeader;
_Static_assert(sizeof(AtfDetailHeader) == 64, "Detail header must be 64 bytes");
```

### Detail Event Header (24 bytes + variable payload)

```c
typedef struct __attribute__((packed)) {
    uint32_t total_length;       // Including this header and payload
    uint16_t event_type;         // FUNCTION_CALL=3, FUNCTION_RETURN=4, etc.
    uint16_t flags;              // Event-specific flags
    uint32_t index_seq;          // Backward link to index event position
    uint32_t thread_id;          // Thread that generated event
    uint64_t timestamp;          // Monotonic nanoseconds (same as index)
    // Payload follows (registers, stack, etc.)
} DetailEventHeader;
```

**Bidirectional Linking:**
- `IndexEvent.detail_seq` → forward link to detail event
- `DetailEventHeader.index_seq` → backward link to index event

### Detail Function Payload (ARM64)

```c
typedef struct __attribute__((packed)) {
    uint64_t function_id;        // Same as index event
    uint64_t x_regs[8];          // x0-x7 (arguments or return value)
    uint64_t lr;                 // Link register
    uint64_t fp;                 // Frame pointer
    uint64_t sp;                 // Stack pointer
    uint16_t stack_size;         // Bytes of stack captured (0-256)
    uint16_t _reserved;
    // uint8_t stack_snapshot[stack_size] follows
} DetailFunctionPayload;
```

### Detail Footer (64 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           // "2DTA" (reversed)
    uint32_t checksum;           // CRC32 of events section
    uint64_t event_count;        // Actual event count
    uint64_t bytes_length;       // Actual bytes in events section
    uint64_t time_start_ns;      // First event timestamp
    uint64_t time_end_ns;        // Last event timestamp
    uint8_t  reserved[24];
} AtfDetailFooter;
_Static_assert(sizeof(AtfDetailFooter) == 64, "Detail footer must be 64 bytes");
```

---

## Timing and Synchronization

### Platform Timing API (Genlock)

All timestamps use platform-specific continuous clocks that are:
- **Cross-core synchronized** - guaranteed by OS
- **Nanosecond precision** - sufficient for function tracing
- **Continuous through sleep** - critical for mobile sessions

```c
// ada_timing.h - Unified cross-platform timing
void ada_timing_init(void);
uint64_t ada_get_timestamp_ns(void);  // Monotonic, continuous through sleep

// Implementation per platform:
// - macOS/iOS:      mach_continuous_time()     ~20-40ns overhead
// - Windows:        QueryPerformanceCounter()   ~20-50ns overhead
// - Linux/Android:  clock_gettime(CLOCK_BOOTTIME) ~50-100ns overhead
```

### Cross-Thread Ordering

The `timestamp_ns` field IS the genlock because:
- Platform APIs provide total ordering across all cores
- No need for separate CPU counter - one timestamp is enough
- Cross-thread merge-sort by `timestamp_ns` gives true causality

**Merging per-thread files for cross-thread analysis:**
```
1. Load all thread index files
2. Merge-sort by timestamp_ns
3. Result: true cross-thread causality
```

### Media Stream Correlation

For audio/screen recording synchronization:
```swift
struct TimingAnchor {
    let hostTimeNs: UInt64        // ada_get_timestamp_ns() at capture
    let mediaTime: CMTime         // CMClockGetTime() at capture
}
```

---

## Bidirectional Navigation

### Counter Reservation at Hook Time

```c
// At function hook (single producer per thread, no locks needed):
void record_event(ThreadState* ts, bool detail_enabled) {
    uint32_t idx_seq = ts->index_count++;  // Reserve index position
    uint32_t det_seq = detail_enabled ? ts->detail_count++ : UINT32_MAX;

    // Write to index ring buffer with forward link
    write_index_event(ts->index_ring, idx_seq, det_seq, ...);

    // Write to detail ring buffer with backward link (if enabled)
    if (detail_enabled) {
        write_detail_event(ts->detail_ring, det_seq, idx_seq, ...);
    }
}
```

### Navigation Examples

```
thread_0/index.atf                      thread_0/detail.atf
──────────────────                      ───────────────────
pos=0, detail_seq=MAX, Call A           (no detail - detail_seq=MAX)
pos=1, detail_seq=0, Return A  ───────→ pos=0, index_seq=1, registers
                               ←───────
pos=2, detail_seq=MAX, Call B           (no detail)
pos=3, detail_seq=1, Return B  ───────→ pos=1, index_seq=3, registers
                               ←───────

Reader API (O(1) in both directions):
  Forward:  detail = detail_reader.get(index_event.detail_seq)
  Backward: index = index_reader.get(detail_event.index_seq)
```

---

## Query Patterns

| Pattern | Method |
|---------|--------|
| Single thread analysis | Read `thread_N/index.atf` directly (no filtering) |
| Thread with detail | Lookup in `thread_N/detail.atf` by `detail_seq` |
| Cross-thread view | Merge-sort all thread files by `timestamp_ns` |
| Time range query | Filter by `timestamp_ns` (use header for fast skip) |
| Forward navigation | index.detail_seq → O(1) detail lookup |
| Backward navigation | detail.index_seq → O(1) index lookup |

---

## Design Rationale

### Why Raw Binary (Not Protobuf)?

1. **Zero encoding overhead** - Direct memcpy from ring buffer to disk
2. **Streaming friendly** - Append-only writes, no framing complexity
3. **Fixed-size index events** - O(1) random access by sequence number
4. **Memory-mappable** - Direct pointer access without deserialization
5. **10M+ events/sec** - Target throughput not achievable with protobuf

### Why Per-Thread Files (Not Interleaved)?

1. **Cache locality** - Thread analysis doesn't pollute cache with other threads
2. **Parallel processing** - Thread files can be processed independently
3. **Simpler recovery** - One corrupt file doesn't affect others
4. **No filtering** - Per-thread queries don't scan irrelevant events
5. **Matches architecture** - Per-thread SPSC ring buffers → per-thread files

### Why Bidirectional Linking?

1. **O(1) lookup** - Both directions are direct lookups, not scans
2. **Zero space cost** - Uses existing padding field in IndexEvent
3. **Symmetric API** - Readers navigate equally in both directions
4. **Sequential writes** - Both files remain append-only during recording
