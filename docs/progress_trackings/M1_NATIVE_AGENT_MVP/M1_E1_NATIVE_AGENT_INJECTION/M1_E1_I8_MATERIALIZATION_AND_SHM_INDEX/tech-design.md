---
id: M1_E1_I8-design
iteration: M1_E1_I8
---
# M1_E1_I8 Technical Design: Address Materialization & SHM Index Registration

## Overview
Introduce a canonical shared-memory (SHM) index directory and implement per-process address materialization using that directory. Both agent and controller register the same SHM indices, ensuring consistent (index, offset) addressing across processes. No cache invalidation; layouts are immutable for the session.

## Architecture

### Canonical SHM Directory (Control Block)
Add a directory published in the control block with stable indices:

```c
typedef struct {
    char     name[64];   // OS SHM name (e.g., /ada_thread_registry)
    uint64_t size;       // bytes
} ShmEntry;

typedef struct {
    uint32_t schema_version; // for future evolution
    uint32_t count;          // number of entries
    ShmEntry entries[8];     // 0 = registry arena; others reserved for future
} ShmDirectory;
```

Initial content for MVP:
- entries[0] = registry arena; `count = 1`.
- Index lane/detail lane multi-buffer segments may be added later using entries[1..].

### Per-Process Registration & Materialization
On attach/init, each process:
1. Reads `ShmDirectory` from control block.
2. Maps each `entries[i].name` to a local base pointer.
3. Stores `base[i]` in a process-local array (indexed by canonical `i`).
4. Materializes addresses as `addr = base[shm_idx] + offset`.

### Accessors
- Use `(shm_idx, offset)` handles from SHM (for I7, registry remains `shm_idx=0`).
- Compute addresses per call as `addr = base[shm_idx] + offset` using inline helpers; do not keep persistent materialized-address caches.
- No invalidation; directory and layouts are immutable for the session.

## Sequences

1) Agent Init:
- Read directory -> map entries -> set local bases.
- Proceed with normal registration; write offsets-only (I6).

2) Controller Attach:
- Read directory -> map entries -> set local bases.
- Iterate registry slots; materialize and drain as needed.

## Success Criteria
1. Both processes share the same SHM indices (verified by names/sizes).
2. Accessors materialize addresses using `(index, offset)`; no absolute pointers in SHM.
3. All unit/integration tests pass without perf regressions post-warmup.

## Risks & Mitigations
- Mapping mismatch: explicit size/name validation and fail-fast paths.
- Directory drift: directory is immutable in MVP; no updates post publish.
