---
id: BH-003
title: ATF Detail File Format
status: active
source: docs/specs/TRACE_SCHEMA.md
---

# ATF Detail File Format

## Context

**Given:**
- Rich context (registers, stack) needs to be captured when detail recording is active
- Detail files are only created when detail recording is activated
- The format must support bidirectional linking back to index events
- Events must be length-prefixed for variable-size payloads
- The file must be compact with no holes

## Trigger

**When:** Detail recording is activated (user presses record, trigger fires, etc.) and detail events are persisted to disk

## Outcome

**Then:**
- A `detail.atf` file is created in the thread directory
- The file contains a 64-byte header, length-prefixed detail events, and a 64-byte footer
- Detail events include a header with backward link to index events via `index_seq`
- Events contain rich context including registers and stack snapshots
- The file is compact with only captured detail events (no holes)
- The file references the corresponding index file

## File Structure

### File Layout
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
```

## Event Structure

### Length-Prefixed Format
Each detail event is self-describing with a total_length field that includes:
- DetailEventHeader (24 bytes)
- Payload (variable size based on event_type)

This allows readers to skip over events or navigate sequentially.

### Register Capture (ARM64)
- x0-x7: First 8 argument registers (on call) or return value registers (on return)
- lr: Link register (return address)
- fp: Frame pointer (for stack unwinding)
- sp: Stack pointer (for stack snapshot base)

### Stack Snapshot
- Configurable size (default 128 bytes, max 256 bytes)
- Captured from stack pointer (SP) location
- Shallow window for context without excessive overhead

## Edge Cases

### Bidirectional Navigation
**Given:** A detail event with an index_seq field
**When:** The reader needs to find the corresponding index event
**Then:**
- Use index_seq to compute byte offset: offset = index_seq * 32
- Read index event directly from index file at that offset
- This is an O(1) operation

### Navigation Example
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

### Variable-Length Events
**Given:** Detail events have variable-length payloads
**When:** A reader needs to navigate through detail events
**Then:**
- Read DetailEventHeader to get total_length
- Skip to next event using: next_offset = current_offset + total_length
- This enables sequential scanning without parsing payloads

### Missing Detail File
**Given:** An index file exists but no detail file was created
**When:** A reader checks the index header
**Then:**
- The `has_detail_file` flag (bit 0 of flags) is set to 0
- Readers know not to look for detail.atf
- All index events have detail_seq = UINT32_MAX (no detail)

## Query Patterns

| Pattern | Method |
|---------|--------|
| Single thread analysis | Read `thread_N/index.atf` directly (no filtering) |
| Thread with detail | Lookup in `thread_N/detail.atf` by `detail_seq` |
| Forward navigation | index.detail_seq → O(1) detail lookup |
| Backward navigation | detail.index_seq → O(1) index lookup |
| Sequential scan | Use total_length to skip events |

## Design Rationale

### Why Length-Prefixed?
- Variable payloads: Stack snapshots vary in size (0-256 bytes)
- Self-describing: Each event contains its own length
- Sequential navigation: Can skip without parsing
- Extensible: New event types can have different payload structures

### Why Bidirectional Linking?
- O(1) lookup: Both directions are direct lookups, not scans
- Zero space cost: Uses existing padding field in IndexEvent
- Symmetric API: Readers navigate equally in both directions
- Sequential writes: Both files remain append-only during recording

## References

- Original: `docs/specs/TRACE_SCHEMA.md` (archived source)
- Related: `BH-002-atf-index-format` (ATF Index Format)
- Related: `BH-001-system-architecture` (System Architecture - two-lane design)
