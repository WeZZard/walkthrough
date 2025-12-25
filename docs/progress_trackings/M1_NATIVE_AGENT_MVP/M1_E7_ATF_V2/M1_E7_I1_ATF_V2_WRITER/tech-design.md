---
id: M1_E7_I1-design
iteration: M1_E7_I1
---

# M1_E7_I1 Technical Design: ATF V2 Writer

## Overview

This iteration implements raw binary ATF v2 file persistence for drained ring buffer snapshots. It replaces the protobuf-based ATF V4 format with a zero-overhead binary format optimized for streaming throughput.

## Goals

1. Write events in raw binary ATF v2 format (two-file architecture)
2. Implement bidirectional linking between index and detail events
3. Achieve 10M+ events/sec write throughput
4. Support crash recovery via footer finalization

## Architecture

### Two Writers Per Thread

```
┌─────────────────────────────────────────────────────────┐
│                    Per-Thread State                      │
│  ┌─────────────────┐  ┌─────────────────┐               │
│  │  Index Counter  │  │ Detail Counter  │               │
│  │   index_seq++   │  │  detail_seq++   │               │
│  └────────┬────────┘  └────────┬────────┘               │
│           │                    │                         │
│  ┌────────▼────────┐  ┌────────▼────────┐               │
│  │  Index Writer   │  │  Detail Writer  │               │
│  │  (always on)    │  │  (on-demand)    │               │
│  └────────┬────────┘  └────────┬────────┘               │
└───────────┼────────────────────┼────────────────────────┘
            │                    │
            ▼                    ▼
    thread_N/index.atf    thread_N/detail.atf
```

### Bidirectional Counter Design

At hook time, both sequences are reserved atomically:

```c
void record_event(ThreadState* ts, bool detail_enabled) {
    uint32_t idx_seq = ts->index_count++;
    uint32_t det_seq = detail_enabled ? ts->detail_count++ : UINT32_MAX;

    // Write index event with forward link
    write_index_event(ts->index_ring, idx_seq, det_seq, ...);

    // Write detail event with backward link (if enabled)
    if (detail_enabled) {
        write_detail_event(ts->detail_ring, det_seq, idx_seq, ...);
    }
}
```

### File Layouts

**Index File (index.atf):**
```
[Header - 64 bytes]     <- Placeholder, updated at finalize
[Index Events]          <- Fixed 32-byte records
[Footer - 64 bytes]     <- Crash recovery
```

**Detail File (detail.atf):**
```
[Header - 64 bytes]     <- References index file
[Detail Events]         <- Length-prefixed, variable size
[Footer - 64 bytes]     <- Crash recovery
```

## Data Structures

### IndexEvent (32 bytes)

```c
typedef struct __attribute__((packed)) {
    uint64_t timestamp_ns;   // Platform continuous clock (genlock)
    uint64_t function_id;    // (moduleId << 32) | symbolIndex
    uint32_t thread_id;      // OS thread identifier
    uint32_t event_kind;     // CALL=1, RETURN=2, EXCEPTION=3
    uint32_t call_depth;     // Call stack depth
    uint32_t detail_seq;     // Forward link (UINT32_MAX = none)
} IndexEvent;
```

### AtfIndexHeader (64 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];       // "ATI2"
    uint8_t  endian;         // 0x01 = little-endian
    uint8_t  version;        // 1
    uint8_t  arch;           // 1=x86_64, 2=arm64
    uint8_t  os;             // 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows
    uint32_t flags;          // Bit 0: has_detail_file
    uint32_t thread_id;      // Thread ID
    uint8_t  clock_type;     // 1=mach_continuous, 2=qpc, 3=boottime
    uint8_t  _reserved1[3];
    uint32_t _reserved2;
    uint32_t event_size;     // 32
    uint32_t event_count;    // Total events
    uint64_t events_offset;  // Offset to first event
    uint64_t footer_offset;  // Offset to footer
    uint64_t time_start_ns;  // First timestamp
    uint64_t time_end_ns;    // Last timestamp
} AtfIndexHeader;
```

### DetailEventHeader (24 bytes + payload)

```c
typedef struct __attribute__((packed)) {
    uint32_t total_length;   // Header + payload
    uint16_t event_type;     // FUNCTION_CALL=3, FUNCTION_RETURN=4
    uint16_t flags;          // Event flags
    uint32_t index_seq;      // Backward link to index event
    uint32_t thread_id;      // Thread ID
    uint64_t timestamp;      // Same as index event
    // Payload follows
} DetailEventHeader;
```

## API Design

### Writer Initialization

```c
// Create writer for a thread
AtfThreadWriter* atf_writer_create(
    const char* session_dir,
    uint32_t thread_id,
    uint8_t clock_type
);

// Close and finalize
void atf_writer_close(AtfThreadWriter* writer);
```

### Event Writing

```c
// Write index event (returns index_seq)
uint32_t atf_write_index_event(
    AtfThreadWriter* writer,
    uint64_t timestamp_ns,
    uint64_t function_id,
    uint32_t event_kind,
    uint32_t call_depth,
    uint32_t detail_seq  // UINT32_MAX if no detail
);

// Write detail event
void atf_write_detail_event(
    AtfThreadWriter* writer,
    uint32_t index_seq,
    uint64_t timestamp_ns,
    const DetailFunctionPayload* payload
);
```

### Finalization

```c
// Update headers and write footers
void atf_writer_finalize(AtfThreadWriter* writer);
```

## Integration Points

### Drain Thread Integration

The drain thread calls the writer API when flushing ring buffers:

```c
void drain_thread_flush(DrainState* state) {
    // For each registered thread
    for (int i = 0; i < state->thread_count; i++) {
        ThreadLaneSet* tls = state->threads[i];

        // Drain index events
        while (ring_buffer_available(tls->index_ring) > 0) {
            IndexEvent event;
            ring_buffer_read(tls->index_ring, &event);
            atf_write_index_event(tls->writer, ...);
        }

        // Drain detail events (if enabled)
        if (tls->detail_enabled) {
            while (ring_buffer_available(tls->detail_ring) > 0) {
                // ... write detail events
            }
        }
    }
}
```

## Performance Considerations

### Target: 10M events/sec

- **Zero encoding**: Direct memcpy from ring buffer
- **Sequential I/O**: Append-only writes
- **Buffered I/O**: Use stdio buffering (64KB)
- **No locks**: Per-thread writers

### Memory Layout

- 32-byte IndexEvent is cache-line friendly (2 events per line)
- Headers/footers are 64 bytes (1 cache line)

## Error Handling

### Crash Recovery

If the process crashes before finalization:
1. Footer contains authoritative event count
2. Reader checks footer magic to detect incomplete files
3. Truncate to last complete event

### Disk Full

- Check write return values
- Close files gracefully on error
- Log warning but don't crash traced process

## References

- **Format Spec**: `BH-002-atf-index-format` (ATF Index Format), `BH-003-atf-detail-format` (ATF Detail Format)
- **Architecture**: `BH-001-system-architecture` (System Architecture)
