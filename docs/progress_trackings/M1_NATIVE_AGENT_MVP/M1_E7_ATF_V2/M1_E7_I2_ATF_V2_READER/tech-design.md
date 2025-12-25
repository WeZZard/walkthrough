---
id: M1_E7_I2-design
iteration: M1_E7_I2
---

# M1_E7_I2 Technical Design: ATF V2 Reader

## Overview

This iteration implements readers for raw binary ATF v2 format. It replaces the protobuf-based ATF reader with direct binary parsing, enabling memory-mapped access and O(1) bidirectional navigation between index and detail events.

## Goals

1. Parse raw binary ATF v2 format (two-file architecture)
2. Support memory-mapped file access for efficiency
3. Enable O(1) bidirectional navigation (index ↔ detail)
4. Support cross-thread merge-sort by timestamp_ns
5. Handle crash recovery via footer validation

## Architecture

### Two Readers Per Thread

```
┌─────────────────────────────────────────────────────────────────┐
│                    Per-Thread Readers                            │
│  ┌─────────────────────────┐  ┌─────────────────────────────┐   │
│  │     Index Reader        │  │      Detail Reader          │   │
│  │  (memory-mapped)        │  │   (memory-mapped)           │   │
│  │                         │  │                             │   │
│  │  get(seq) → IndexEvent  │  │  get(seq) → DetailEvent    │   │
│  │  iter() → Iterator      │  │  get_by_index(idx) → Event │   │
│  └────────────┬────────────┘  └──────────────┬──────────────┘   │
│               │                              │                   │
│               │     Bidirectional Nav        │                   │
│               │  ◄─────────────────────────► │                   │
│               │   forward: detail_seq        │                   │
│               │   backward: index_seq        │                   │
└───────────────┼──────────────────────────────┼───────────────────┘
                │                              │
                ▼                              ▼
        thread_N/index.atf             thread_N/detail.atf
```

### Session Reader

```
┌─────────────────────────────────────────────────────────────────┐
│                     Session Reader                               │
│  ┌─────────────┐                                                │
│  │  Manifest   │  ← manifest.json (thread list, time range)     │
│  └─────────────┘                                                │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │  Thread 0   │  │  Thread 1   │  │  Thread N   │   ...        │
│  │  Readers    │  │  Readers    │  │  Readers    │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              Merge-Sort Iterator                          │   │
│  │         (cross-thread by timestamp_ns)                    │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Data Structures (Rust)

### Index Reader

```rust
pub struct IndexReader {
    mmap: Mmap,
    header: AtfIndexHeader,
    footer: AtfIndexFooter,
    events_offset: usize,
    event_count: u32,
}

impl IndexReader {
    pub fn open(path: &Path) -> Result<Self, AtfError>;

    /// Get event by sequence number (O(1))
    pub fn get(&self, seq: u32) -> Option<&IndexEvent>;

    /// Get event count
    pub fn len(&self) -> u32;

    /// Check if detail file exists
    pub fn has_detail(&self) -> bool;

    /// Iterate all events
    pub fn iter(&self) -> IndexEventIter<'_>;

    /// Time range
    pub fn time_range(&self) -> (u64, u64);
}
```

### Detail Reader

```rust
pub struct DetailReader {
    mmap: Mmap,
    header: AtfDetailHeader,
    footer: AtfDetailFooter,
    events_offset: usize,
    /// Index of detail events by sequence for O(1) lookup
    event_index: Vec<usize>,  // seq -> byte offset
}

impl DetailReader {
    pub fn open(path: &Path) -> Result<Self, AtfError>;

    /// Get detail event by sequence number (O(1))
    pub fn get(&self, detail_seq: u32) -> Option<DetailEvent<'_>>;

    /// Get detail event by its linked index sequence
    pub fn get_by_index_seq(&self, index_seq: u32) -> Option<DetailEvent<'_>>;

    /// Iterate all detail events
    pub fn iter(&self) -> DetailEventIter<'_>;
}
```

### Bidirectional Navigation

```rust
pub struct ThreadReader {
    pub index: IndexReader,
    pub detail: Option<DetailReader>,
}

impl ThreadReader {
    pub fn open(thread_dir: &Path) -> Result<Self, AtfError>;

    /// Forward lookup: index event → paired detail event (O(1))
    pub fn get_detail_for(&self, index_event: &IndexEvent) -> Option<DetailEvent<'_>> {
        if index_event.detail_seq == u32::MAX {
            return None;
        }
        self.detail.as_ref()?.get(index_event.detail_seq)
    }

    /// Backward lookup: detail event → paired index event (O(1))
    pub fn get_index_for(&self, detail_event: &DetailEvent<'_>) -> Option<&IndexEvent> {
        self.index.get(detail_event.header().index_seq)
    }
}
```

### Session Reader with Merge-Sort

```rust
pub struct SessionReader {
    manifest: Manifest,
    threads: Vec<ThreadReader>,
}

impl SessionReader {
    pub fn open(session_dir: &Path) -> Result<Self, AtfError>;

    /// Get all thread readers
    pub fn threads(&self) -> &[ThreadReader];

    /// Merge-sort iterator across all threads by timestamp_ns
    pub fn merged_iter(&self) -> MergedEventIter<'_>;

    /// Time range across all threads
    pub fn time_range(&self) -> (u64, u64);

    /// Total event count
    pub fn event_count(&self) -> u64;
}

/// Merge-sort iterator using min-heap
pub struct MergedEventIter<'a> {
    heap: BinaryHeap<Reverse<(u64, usize, u32)>>,  // (timestamp, thread_idx, seq)
    threads: &'a [ThreadReader],
}
```

## Python Implementation

### Index Reader (Python)

```python
class IndexReader:
    """Memory-mapped reader for ATF v2 index files."""

    def __init__(self, path: Path):
        self._mmap = mmap.mmap(...)
        self._header = self._parse_header()
        self._footer = self._parse_footer()

    def __len__(self) -> int:
        return self._header.event_count

    def __getitem__(self, seq: int) -> IndexEvent:
        """O(1) access by sequence number."""
        offset = self._header.events_offset + seq * 32
        return IndexEvent.from_bytes(self._mmap[offset:offset+32])

    def __iter__(self) -> Iterator[IndexEvent]:
        for seq in range(len(self)):
            yield self[seq]

    @property
    def has_detail(self) -> bool:
        return bool(self._header.flags & 0x01)
```

### Detail Reader (Python)

```python
class DetailReader:
    """Memory-mapped reader for ATF v2 detail files."""

    def __init__(self, path: Path):
        self._mmap = mmap.mmap(...)
        self._header = self._parse_header()
        self._event_index = self._build_index()  # seq -> offset

    def get(self, detail_seq: int) -> Optional[DetailEvent]:
        """O(1) access by detail sequence number."""
        if detail_seq >= len(self._event_index):
            return None
        offset = self._event_index[detail_seq]
        return DetailEvent.from_mmap(self._mmap, offset)

    def get_by_index_seq(self, index_seq: int) -> Optional[DetailEvent]:
        """Find detail event by its linked index sequence (O(n) scan)."""
        for event in self:
            if event.index_seq == index_seq:
                return event
        return None
```

### Bidirectional Navigation (Python)

```python
class ThreadReader:
    """Combined reader for thread index + detail files."""

    def __init__(self, thread_dir: Path):
        self.index = IndexReader(thread_dir / "index.atf")
        detail_path = thread_dir / "detail.atf"
        self.detail = DetailReader(detail_path) if detail_path.exists() else None

    def get_detail_for(self, index_event: IndexEvent) -> Optional[DetailEvent]:
        """Forward lookup: index → detail (O(1))."""
        if index_event.detail_seq == 0xFFFFFFFF:
            return None
        return self.detail.get(index_event.detail_seq) if self.detail else None

    def get_index_for(self, detail_event: DetailEvent) -> IndexEvent:
        """Backward lookup: detail → index (O(1))."""
        return self.index[detail_event.index_seq]
```

## File Format Parsing

### Header Validation

```rust
fn validate_index_header(header: &AtfIndexHeader) -> Result<(), AtfError> {
    // Magic check
    if &header.magic != b"ATI2" {
        return Err(AtfError::InvalidMagic);
    }

    // Version check
    if header.version != 1 {
        return Err(AtfError::UnsupportedVersion(header.version));
    }

    // Endianness check
    if header.endian != 0x01 {
        return Err(AtfError::UnsupportedEndian);
    }

    // Event size check
    if header.event_size != 32 {
        return Err(AtfError::InvalidEventSize(header.event_size));
    }

    Ok(())
}
```

### Footer-Based Recovery

```rust
fn recover_event_count(mmap: &Mmap, header: &AtfIndexHeader) -> u32 {
    // Try to read footer
    let footer_offset = header.footer_offset as usize;
    if footer_offset + 64 <= mmap.len() {
        let footer: &AtfIndexFooter = ...;
        if &footer.magic == b"2ITA" {
            // Footer is valid, use its count (authoritative)
            return footer.event_count as u32;
        }
    }

    // Footer invalid/incomplete, calculate from file size
    let events_section = mmap.len() - header.events_offset as usize;
    (events_section / 32) as u32
}
```

## Cross-Thread Merge-Sort

### Algorithm

```rust
impl<'a> Iterator for MergedEventIter<'a> {
    type Item = (usize, &'a IndexEvent);  // (thread_idx, event)

    fn next(&mut self) -> Option<Self::Item> {
        // Pop smallest timestamp from heap
        let Reverse((_, thread_idx, seq)) = self.heap.pop()?;

        let event = self.threads[thread_idx].index.get(seq)?;

        // Push next event from same thread if available
        if let Some(next_event) = self.threads[thread_idx].index.get(seq + 1) {
            self.heap.push(Reverse((next_event.timestamp_ns, thread_idx, seq + 1)));
        }

        Some((thread_idx, event))
    }
}
```

### Initialization

```rust
impl<'a> MergedEventIter<'a> {
    fn new(threads: &'a [ThreadReader]) -> Self {
        let mut heap = BinaryHeap::new();

        // Seed heap with first event from each thread
        for (idx, thread) in threads.iter().enumerate() {
            if let Some(event) = thread.index.get(0) {
                heap.push(Reverse((event.timestamp_ns, idx, 0)));
            }
        }

        Self { heap, threads }
    }
}
```

## Error Handling

### Error Types

```rust
#[derive(Debug, thiserror::Error)]
pub enum AtfError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Invalid magic bytes")]
    InvalidMagic,

    #[error("Unsupported version: {0}")]
    UnsupportedVersion(u8),

    #[error("Unsupported endianness")]
    UnsupportedEndian,

    #[error("Invalid event size: {0}")]
    InvalidEventSize(u32),

    #[error("Sequence out of bounds: {0}")]
    SeqOutOfBounds(u32),

    #[error("Corrupted footer")]
    CorruptedFooter,

    #[error("Missing detail file")]
    MissingDetail,
}
```

## Performance Considerations

### Memory Mapping

- Use `mmap` for zero-copy access to file contents
- 32-byte IndexEvent allows 2 events per cache line
- Sequential iteration is cache-friendly

### Detail Index Building

For O(1) detail lookup by sequence, build an index on open:

```rust
fn build_detail_index(mmap: &Mmap, header: &AtfDetailHeader) -> Vec<usize> {
    let mut index = Vec::with_capacity(header.event_count as usize);
    let mut offset = header.events_offset as usize;

    while offset < mmap.len() - 64 {  // Leave room for footer
        index.push(offset);
        let total_length = u32::from_le_bytes(...);
        offset += total_length as usize;
    }

    index
}
```

### Time Range Queries

```rust
impl IndexReader {
    /// Binary search for first event >= start_ns
    pub fn find_start(&self, start_ns: u64) -> u32 {
        // Binary search on sorted timestamps
        let mut lo = 0;
        let mut hi = self.event_count;
        while lo < hi {
            let mid = (lo + hi) / 2;
            if self.get(mid).unwrap().timestamp_ns < start_ns {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        lo
    }
}
```

## Integration Points

### Query Engine Integration

```rust
// In query_engine/src/atf/mod.rs
pub use v2::{
    SessionReader,
    ThreadReader,
    IndexReader,
    DetailReader,
    IndexEvent,
    DetailEvent,
};
```

### Python Bindings (PyO3)

```rust
#[pyclass]
struct PySessionReader {
    inner: SessionReader,
}

#[pymethods]
impl PySessionReader {
    #[new]
    fn new(path: &str) -> PyResult<Self> { ... }

    fn event_count(&self) -> u64 { ... }

    fn iter_events(&self) -> PyResult<PyEventIterator> { ... }

    fn get_detail_for(&self, thread_idx: usize, seq: u32) -> PyResult<Option<PyDetailEvent>> { ... }
}
```

## References

- **Format Spec**: `BH-002-atf-index-format` (ATF Index Format), `BH-003-atf-detail-format` (ATF Detail Format)
- **Architecture**: `BH-001-system-architecture` (System Architecture)
- **Writer**: M1_E5_I1 (ATF V2 Writer)
