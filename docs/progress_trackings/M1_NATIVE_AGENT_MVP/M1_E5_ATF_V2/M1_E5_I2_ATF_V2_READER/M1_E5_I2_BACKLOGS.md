---
status: active
date: 2025-12-23
supersedes: M1_E4_I1_ATF_READER
---

# M1_E5_I2 Backlogs: ATF V2 Reader

## Sprint Overview

**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - closes M1 MVP loop)
**Dependencies**: M1_E5_I1 (ATF V2 Writer)

## Day 1: Rust Index Reader (8 hours)

### ATF2-R-001: Define Reader Types (P0, 1h)

**File**: `query_engine/src/atf/v2/types.rs`

- [ ] Define AtfIndexHeader struct matching C definition
- [ ] Define IndexEvent struct (32 bytes)
- [ ] Define AtfIndexFooter struct
- [ ] Define AtfDetailHeader struct
- [ ] Define DetailEventHeader struct
- [ ] Add bytemuck derives for zero-copy parsing
- [ ] Add AtfError enum with all error types

### ATF2-R-002: Index Reader Core (P0, 2.5h)

**File**: `query_engine/src/atf/v2/index.rs`

- [ ] Implement IndexReader::open() with mmap
- [ ] Implement header validation
- [ ] Implement footer-based recovery
- [ ] Implement get(seq) for O(1) access
- [ ] Implement len() returning event count
- [ ] Implement has_detail() flag check
- [ ] Implement time_range() getter

### ATF2-R-003: Index Iterator (P0, 1.5h)

**File**: `query_engine/src/atf/v2/index.rs`

- [ ] Implement IndexEventIter struct
- [ ] Implement Iterator trait for sequential access
- [ ] Add ExactSizeIterator implementation
- [ ] Add DoubleEndedIterator for reverse iteration
- [ ] Verify cache-friendly access pattern

### ATF2-R-004: Binary Search for Time Range (P0, 1h)

**File**: `query_engine/src/atf/v2/index.rs`

- [ ] Implement find_start(timestamp) binary search
- [ ] Implement find_end(timestamp) binary search
- [ ] Implement range(start_ns, end_ns) iterator
- [ ] Add tests for edge cases (empty, single event)

### ATF2-R-005: Unit Tests - Index Reader (P0, 2h)

**File**: `query_engine/tests/atf_v2_index_tests.rs`

- [ ] Test header parsing (valid/invalid)
- [ ] Test get() with various sequences
- [ ] Test out-of-bounds access
- [ ] Test footer recovery
- [ ] Test iteration correctness
- [ ] Test binary search

## Day 2: Rust Detail Reader & Bidirectional Navigation (8 hours)

### ATF2-R-006: Detail Reader Core (P0, 2h)

**File**: `query_engine/src/atf/v2/detail.rs`

- [ ] Implement DetailReader::open() with mmap
- [ ] Implement header validation
- [ ] Build event index (seq → byte offset) on open
- [ ] Implement get(detail_seq) for O(1) access
- [ ] Implement get_by_index_seq() for backward lookup
- [ ] Parse length-prefixed variable-size events

### ATF2-R-007: Detail Event Parsing (P0, 1.5h)

**File**: `query_engine/src/atf/v2/detail.rs`

- [ ] Implement DetailEvent<'a> with lifetime for mmap ref
- [ ] Parse DetailEventHeader (24 bytes)
- [ ] Parse DetailFunctionPayload (variable size)
- [ ] Handle stack snapshot parsing
- [ ] Implement iterator for detail events

### ATF2-R-008: Thread Reader (P0, 1.5h)

**File**: `query_engine/src/atf/v2/thread.rs`

- [ ] Implement ThreadReader combining index + detail
- [ ] Implement get_detail_for(index_event) forward lookup
- [ ] Implement get_index_for(detail_event) backward lookup
- [ ] Handle index-only case (no detail file)
- [ ] Verify O(1) bidirectional navigation

### ATF2-R-009: Session Reader (P0, 1.5h)

**File**: `query_engine/src/atf/v2/session.rs`

- [ ] Implement SessionReader::open(session_dir)
- [ ] Parse manifest.json for thread list
- [ ] Create ThreadReader for each thread directory
- [ ] Implement threads() accessor
- [ ] Implement time_range() across all threads
- [ ] Implement event_count() total

### ATF2-R-010: Cross-Thread Merge-Sort (P0, 1.5h)

**File**: `query_engine/src/atf/v2/merge.rs`

- [ ] Implement MergedEventIter with min-heap
- [ ] Seed heap with first event from each thread
- [ ] Implement Iterator trait for merged iteration
- [ ] Return (thread_idx, &IndexEvent) tuples
- [ ] Add benchmark for merge performance

## Day 3: Python Bindings & Integration (8 hours)

### ATF2-R-011: Python Index Reader (P0, 2h)

**File**: `query_engine/python/query_engine/atf/v2/index.py`

- [ ] Implement IndexReader class with mmap
- [ ] Implement __len__() for event count
- [ ] Implement __getitem__(seq) for O(1) access
- [ ] Implement __iter__() for sequential access
- [ ] Implement time_range property
- [ ] Implement has_detail property

### ATF2-R-012: Python Detail Reader (P0, 1.5h)

**File**: `query_engine/python/query_engine/atf/v2/detail.py`

- [ ] Implement DetailReader class with mmap
- [ ] Build event index on open
- [ ] Implement get(detail_seq)
- [ ] Implement __iter__() for iteration
- [ ] Parse variable-length events

### ATF2-R-013: Python Thread Reader (P0, 1h)

**File**: `query_engine/python/query_engine/atf/v2/thread.py`

- [ ] Implement ThreadReader combining readers
- [ ] Implement get_detail_for() forward lookup
- [ ] Implement get_index_for() backward lookup
- [ ] Handle index-only case

### ATF2-R-014: Python Session Reader (P0, 1h)

**File**: `query_engine/python/query_engine/atf/v2/session.py`

- [ ] Implement SessionReader class
- [ ] Parse manifest.json
- [ ] Create ThreadReader for each thread
- [ ] Implement merged iteration (heapq)

### ATF2-R-015: Integration Tests (P0, 2.5h)

**Files**: `query_engine/tests/integration/`

- [ ] Test round-trip: C writer → Rust reader
- [ ] Test round-trip: C writer → Python reader
- [ ] Test bidirectional links valid
- [ ] Test cross-thread merge-sort ordering
- [ ] Test index-only sessions
- [ ] Test corrupted file handling
- [ ] Test large file handling (10M+ events)

## Cleanup Tasks

### ATF2-R-016: Remove Protobuf Dependencies (P1, 1h)

- [ ] Remove prost from query_engine/Cargo.toml
- [ ] Delete old ATF V4 reader code
- [ ] Update Python dependencies (remove protobuf)
- [ ] Verify clean build

### ATF2-R-017: Update Query Engine API (P1, 1h)

- [ ] Export v2 reader types from query_engine crate
- [ ] Update existing query engine code to use v2
- [ ] Ensure JSON-RPC API remains compatible
- [ ] Update documentation

## Definition of Done

- [ ] Index reader parses all valid ATF v2 files
- [ ] Detail reader handles variable-length events
- [ ] O(1) bidirectional navigation works (forward + backward)
- [ ] Cross-thread merge-sort produces correct ordering
- [ ] Python bindings work for all reader functionality
- [ ] Round-trip tests pass (writer → reader)
- [ ] Sequential throughput exceeds 1 GB/sec
- [ ] Random access latency under 1 microsecond
- [ ] Error handling graceful for corrupted files
- [ ] Unit tests pass (100% coverage on new code)
- [ ] Integration tests pass

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Memory mapping compatibility | High | Low | Use proven memmap2 crate |
| Detail index memory usage | Medium | Medium | Lazy loading option |
| Merge-sort heap overhead | Low | Low | Pre-allocate with capacity |
| Python binding performance | Medium | Low | Use numpy arrays for bulk access |
| Endianness issues | High | Low | Test on both architectures |

## Dependencies

### Depends On:
- M1_E5_I1: ATF V2 Writer (produces files to read)

### Depended By:
- Query Engine JSON-RPC API (consumes reader)
- MCP Server (uses query engine)
