---
id: M1_E1_I6-design
iteration: M1_E1_I6
---
# M1_E1_I6 Technical Design: Offset-Only SHM (Full Scale)

## Overview
Convert all shared-memory (SHM) structures used by the ThreadRegistry and per-thread lanes to use offsets only. Remove all absolute pointers from SHM. Each process computes real addresses from offsets using its local SHM base on every access (no persistent materialized-address cache). No cache invalidation or generations are introduced in this iteration; layouts are immutable for the session.

Key goals:
- SHM contains only portable data: offsets, sizes, counters, and flags.
- Address materialization is performed per call; helpers are inline and cache-friendly; no persistent caches are kept.
- No runtime frees/moves; no cache invalidation required.

## Architecture

### Data Model Changes
- Remove from SHM:
  - `Lane*` or `LaneMemoryLayout*` absolute pointers
  - `LaneMemoryLayout.ring_ptrs[]`
  - `LaneMemoryLayout.ring_memory_base`
  - `LaneMemoryLayout.rb_handles[]`
- Add/Keep in SHM:
  - `layout_off` (uint64): offset from registry base to lane layout
  - `RingDescriptor` per ring: `{ bytes: u32, offset: u64 }`
  - SPSC queues: `submit_queue[]`, `free_queue[]`, and their head/tail indices
  - Lane state: `active_idx`, `ring_count`, metrics
  - Thread slot: `thread_id`, `slot_index`, `active`

SHM remains a single arena in this iteration. All offsets are relative to the registry arena base.

### Address Materialization (Per Call)
- For each write/read/queue op:
  1. Compute `LaneMemoryLayout* layout = base + layout_off` (two adds land in L1).
  2. Compute ring address per needed ring: `ring_addr = base + ring_desc[offset]`.
  3. Use header-only raw ring operations that act directly on `RingBufferHeader` and payload memory (no heap objects, no stored handles).
- All helpers are `inline` and designed to be cache-friendly; no persistent materialized addresses are stored between calls.

### Raw Ring Helpers (Header-Only)
- Introduced C helpers for offsets-only consumers/producers:
  - `ring_buffer_write_raw`, `ring_buffer_read_raw`, `ring_buffer_read_batch_raw`
  - `ring_buffer_available_read_raw`, `ring_buffer_available_write_raw`
- These operate on a `RingBufferHeader*` and adjacent payload region; used by agent and controller.

### Registration (Writer)
- On slot allocation, writer (agent) initializes lane layouts by:
  - Allocating layout and ring regions out of the SHM arena bump allocator.
  - Writing `layout_off` and `ring_descs[i] {bytes, offset}`.
  - Initializing queues and metadata.
  - Marking `active=true` with release semantics.

### Memory Ordering
- Same as existing SPSC queues and ring headers.
- No special ordering for materialization: it reads immutable layout metadata.

## Data Structures

```c
// Stored in SHM
typedef struct {
    uint32_t bytes;   // size of ring in bytes
    uint64_t offset;  // offset from registry base
} RingDescriptor;

typedef struct {
    // No absolute pointers in SHM
    // Ring descriptors replace ring_ptrs[] and ring_memory_base
    RingDescriptor ring_descs[RINGS_PER_INDEX_LANE];

    // SPSC queues
    uint32_t submit_queue[QUEUE_COUNT_INDEX_LANE];
    uint32_t free_queue[QUEUE_COUNT_INDEX_LANE];
} LaneMemoryLayout_SHM;

typedef struct {
    // Thread id and slot index
    uintptr_t thread_id;
    uint32_t slot_index;
    _Atomic(bool) active;

    // Offsets into SHM for lane layouts
    uint64_t index_layout_off;
    uint64_t detail_layout_off;

    // Lane state & metrics (no pointers)
    _Atomic(uint32_t) index_active_idx;
    _Atomic(uint32_t) detail_active_idx;
    // ... counters elided for brevity ...
} ThreadLaneSet_SHM;
```

Implementation note:
- In-process producer/consumer code does not store absolute pointers in SHM. Where a process-local pointer is needed, it is materialized transiently from offsets (e.g., in agent/controller fast paths) via a helper that returns a `RingBufferHeader*` from `(registry_base + segment.base_offset + layout.ring_desc[i].offset)`.

## Examples

1) Layout materialization from offsets
- Given a `ThreadLaneSet` with `index_layout_off` and a known `registry_base` and `segments[0].base_offset`, compute:
  - `auto* layout = (LaneMemoryLayout*)(registry_base + segments[0].base_offset + index_layout_off);`
- Verify queue access by writing/reading `submit_queue[0]`.

2) Attach using ring descriptor offsets (no stored handle)
- From `layout->ring_descs[idx]` compute:
  - `uint8_t* ring_ptr = seg_base + desc.offset;`
  - Attach a temporary handle: `ring_buffer_attach(ring_ptr, desc.bytes, sizeof(IndexEvent))` to validate round-trip.

3) Raw header-only round-trip
- Compute `RingBufferHeader* hdr = (RingBufferHeader*)(seg_base + desc.offset);`
- Use `ring_buffer_write_raw(hdr, sizeof(IndexEvent), &ev)` then `ring_buffer_read_raw(...)` and assert equality.

These examples are codified in `tests/unit/utils/test_offsets_materialization.cpp` and exercised in CI.

## Accessor Changes
- `lane_submit_ring`, `lane_take_ring`, `lane_return_ring`, `lane_swap_active_ring` compute queue addresses from `layout_off` per call.
- Event emission uses inline raw ring helpers on `(base + ring_desc[i].offset)` without persistent handles.

## Success Criteria
1. No absolute pointers in SHM after registration.
2. Accessors function correctly using offsets-only SHM.
3. No measurable hot-path regression after warmup; registration cost unchanged. Target remains < 10ns avg per cached scenario even with per-call address computation; verify via perf tests.
4. All existing unit/integration/perf tests pass with updated assertions.

Status: Verified via unit tests (`test_thread_registry`, `test_offsets_materialization`) and integration tests (`test_thread_registry_integration`).

## Risks & Mitigations
- Risk: Missed pointer fields in SHM. Mitigation: add tests asserting zeroed pointer fields and presence of offsets.
- Risk: Extra pointer arithmetic per call. Mitigation: inline, branch-light helpers; event copy dominates; verify with perf.
- Risk: Attach errors. Mitigation: explicit error paths and tests.
