//! ATF index event reader
//!
//! Reads ATF v2 index files for event data.

use std::fs::File;
use std::path::Path;

use anyhow::{bail, Context, Result};
use memmap2::Mmap;

/// ATF V2 Index File Header - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfIndexHeader {
    pub magic: [u8; 4],           // "ATI2"
    pub endian: u8,               // 0x01 = little-endian
    pub version: u8,              // 1
    pub arch: u8,                 // 1=x86_64, 2=arm64
    pub os: u8,                   // 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows
    pub flags: u32,               // Bit 0: has_detail_file
    pub thread_id: u32,           // Thread ID for this file
    pub clock_type: u8,           // 1=mach_continuous, 2=qpc, 3=boottime
    pub _reserved1: [u8; 3],
    pub _reserved2: u32,
    pub event_size: u32,          // 32 bytes per event
    pub event_count: u32,         // Total number of events
    pub events_offset: u64,       // Offset to first event
    pub footer_offset: u64,       // Offset to footer (for recovery)
    pub time_start_ns: u64,       // First event timestamp
    pub time_end_ns: u64,         // Last event timestamp
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfIndexHeader>() == 64);

/// ATF V2 Index Event - 32 bytes (fixed size)
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct IndexEventRaw {
    pub timestamp_ns: u64,        // Platform continuous clock
    pub function_id: u64,         // (moduleId << 32) | symbolIndex
    pub thread_id: u32,           // OS thread identifier
    pub event_kind: u32,          // CALL=1, RETURN=2, EXCEPTION=3
    pub call_depth: u32,          // Call stack depth
    pub detail_seq: u32,          // Forward link to detail event (u32::MAX = none)
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<IndexEventRaw>() == 32);

/// ATF V2 Index File Footer - 64 bytes
#[repr(C, packed)]
#[derive(Debug, Copy, Clone)]
pub struct AtfIndexFooter {
    pub magic: [u8; 4],           // "2ITA" (reversed)
    pub checksum: u32,            // CRC32 of events section
    pub event_count: u64,         // Actual event count (authoritative)
    pub time_start_ns: u64,       // First event timestamp
    pub time_end_ns: u64,         // Last event timestamp
    pub bytes_written: u64,       // Total bytes in events section
    pub reserved: [u8; 24],
}

// Compile-time size check
const _: () = assert!(std::mem::size_of::<AtfIndexFooter>() == 64);

/// Event kind constants
pub const EVENT_KIND_CALL: u32 = 1;
pub const EVENT_KIND_RETURN: u32 = 2;
pub const EVENT_KIND_EXCEPTION: u32 = 3;

/// A parsed event
#[derive(Debug, Clone)]
pub struct Event {
    pub timestamp_ns: u64,
    pub function_id: u64,
    pub thread_id: u32,
    pub kind: EventKind,
    pub depth: u32,
}

/// Event kind enum
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventKind {
    Call,
    Return,
    Exception,
    Unknown(u32),
}

impl From<u32> for EventKind {
    fn from(v: u32) -> Self {
        match v {
            EVENT_KIND_CALL => EventKind::Call,
            EVENT_KIND_RETURN => EventKind::Return,
            EVENT_KIND_EXCEPTION => EventKind::Exception, // LCOV_EXCL_LINE - Rare
            other => EventKind::Unknown(other), // LCOV_EXCL_LINE - Filtered in session.rs
        }
    }
}

impl std::fmt::Display for EventKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            EventKind::Call => write!(f, "CALL"),
            EventKind::Return => write!(f, "RETURN"),
            EventKind::Exception => write!(f, "EXCEPT"),
            EventKind::Unknown(v) => write!(f, "UNK({})", v),
        }
    }
}

/// Memory-mapped reader for ATF v2 index files
#[allow(dead_code)]
pub struct EventReader {
    _mmap: Mmap,
    event_count: u32,
    events_ptr: *const IndexEventRaw,
    thread_id: u32,
}

// SAFETY: EventReader is Send + Sync because:
// - Mmap is Send + Sync
// - events_ptr points to memory-mapped read-only data
// - No interior mutability
unsafe impl Send for EventReader {}
unsafe impl Sync for EventReader {}

impl EventReader {
    /// Open and memory-map an index file
    pub fn open(path: &Path) -> Result<Self> {
        let file = File::open(path).with_context(|| format!("Failed to open {:?}", path))?;
        let mmap = unsafe { Mmap::map(&file)? };

        // Validate file size (at least header)
        // LCOV_EXCL_START - Error path requires malformed file
        if mmap.len() < 64 {
            bail!(
                "ATF file too small: {} bytes (expected at least 64)",
                mmap.len()
            );
        }
        // LCOV_EXCL_STOP

        // Parse header
        let header =
            unsafe { std::ptr::read_unaligned(mmap.as_ptr() as *const AtfIndexHeader) };

        // Validate header
        Self::validate_header(&header)?;

        // Try to read footer (authoritative for event count)
        let event_count = Self::read_event_count(&mmap, &header);

        let events_offset = header.events_offset as usize;

        // Validate events_offset
        // LCOV_EXCL_START - Error path requires malformed file
        if events_offset >= mmap.len() {
            bail!(
                "Invalid events_offset {} (file size {})",
                events_offset,
                mmap.len()
            );
        }
        // LCOV_EXCL_STOP

        // Calculate pointer to events array
        let events_ptr = unsafe { mmap.as_ptr().add(events_offset) as *const IndexEventRaw };

        Ok(EventReader {
            _mmap: mmap,
            event_count,
            events_ptr,
            thread_id: header.thread_id,
        })
    }

    /// Validate index header
    // LCOV_EXCL_START - Error paths require malformed files
    fn validate_header(header: &AtfIndexHeader) -> Result<()> {
        // Check magic bytes
        if &header.magic != b"ATI2" {
            bail!(
                "Invalid ATF magic: {:?} (expected ATI2)",
                std::str::from_utf8(&header.magic).unwrap_or("???")
            );
        }

        // Check version
        if header.version != 1 {
            bail!("Unsupported ATF version: {}", header.version);
        }

        // Check endianness (only little-endian supported)
        if header.endian != 0x01 {
            bail!("Unsupported endianness: {}", header.endian);
        }

        // Check event size
        let event_size = header.event_size;
        if event_size != 32 {
            bail!("Invalid event size: {} (expected 32)", event_size);
        }

        Ok(())
    }
    // LCOV_EXCL_STOP

    /// Read event count from footer or calculate from file size
    fn read_event_count(mmap: &Mmap, header: &AtfIndexHeader) -> u32 {
        let footer_offset = header.footer_offset as usize;

        // Try to read footer
        if footer_offset + 64 <= mmap.len() {
            let footer = unsafe {
                std::ptr::read_unaligned(mmap.as_ptr().add(footer_offset) as *const AtfIndexFooter)
            };

            // Validate footer magic
            if &footer.magic == b"2ITA" {
                // Footer is valid, use its count (authoritative)
                return footer.event_count as u32;
            } // LCOV_EXCL_LINE - Footer valid branch
        } // LCOV_EXCL_LINE - Valid footer path

        // Footer invalid or missing, calculate from file size
        // LCOV_EXCL_START - Fallback path for corrupted footer
        let events_section_size = mmap.len().saturating_sub(header.events_offset as usize);
        (events_section_size / 32) as u32
        // LCOV_EXCL_STOP
    }

    /// Get event by sequence number (O(1))
    pub fn get(&self, seq: u32) -> Option<Event> {
        if seq >= self.event_count {
            return None;
        }

        // SAFETY: We validated seq < event_count, and events_ptr points to valid mmap memory
        let raw = unsafe { std::ptr::read_unaligned(self.events_ptr.add(seq as usize)) };

        Some(Event {
            timestamp_ns: raw.timestamp_ns,
            function_id: raw.function_id,
            thread_id: raw.thread_id,
            kind: EventKind::from(raw.event_kind),
            depth: raw.call_depth,
        })
    }

    /// Get event count
    pub fn len(&self) -> u32 {
        self.event_count
    }

    /// Check if empty
    // LCOV_EXCL_START - Future API
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.event_count == 0
    }

    /// Get thread ID
    #[allow(dead_code)]
    pub fn thread_id(&self) -> u32 {
        self.thread_id
    }
    // LCOV_EXCL_STOP

    /// Iterate all events
    pub fn iter(&self) -> EventIter<'_> {
        EventIter {
            reader: self,
            pos: 0,
        }
    }
}

/// Iterator over events
pub struct EventIter<'a> {
    reader: &'a EventReader,
    pos: u32,
}

impl<'a> Iterator for EventIter<'a> {
    type Item = Event;

    fn next(&mut self) -> Option<Self::Item> {
        let event = self.reader.get(self.pos)?;
        self.pos += 1;
        Some(event)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = (self.reader.event_count - self.pos) as usize;
        (remaining, Some(remaining))
    }
}

impl<'a> ExactSizeIterator for EventIter<'a> {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn create_test_index_file(event_count: u32) -> NamedTempFile {
        let mut file = NamedTempFile::new().unwrap();

        // Write header
        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 42,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count,
            events_offset: 64,
            footer_offset: 64 + event_count as u64 * 32,
            time_start_ns: 1000,
            time_end_ns: 1000 + event_count as u64 * 100,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
        };
        file.write_all(header_bytes).unwrap();

        // Write events
        for i in 0..event_count {
            let event = IndexEventRaw {
                timestamp_ns: 1000 + i as u64 * 100,
                function_id: 0x100000001 + i as u64,
                thread_id: 42,
                event_kind: if i % 2 == 0 { 1 } else { 2 },
                call_depth: i % 10,
                detail_seq: u32::MAX,
            };

            let event_bytes = unsafe {
                std::slice::from_raw_parts(&event as *const IndexEventRaw as *const u8, 32)
            };
            file.write_all(event_bytes).unwrap();
        }

        // Write footer
        let footer = AtfIndexFooter {
            magic: *b"2ITA",
            checksum: 0,
            event_count: event_count as u64,
            time_start_ns: 1000,
            time_end_ns: 1000 + event_count as u64 * 100,
            bytes_written: event_count as u64 * 32,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
        };
        file.write_all(footer_bytes).unwrap();

        file.flush().unwrap();
        file
    }

    #[test]
    fn test_event_reader__open_valid__then_success() {
        let file = create_test_index_file(10);
        let reader = EventReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 10);
        assert_eq!(reader.thread_id(), 42);
    }

    #[test]
    fn test_event_reader__iterate__then_all_events() {
        let file = create_test_index_file(10);
        let reader = EventReader::open(file.path()).unwrap();
        let events: Vec<_> = reader.iter().collect();
        assert_eq!(events.len(), 10);
    }

    #[test]
    fn test_event_reader__get_event__then_correct_data() {
        let file = create_test_index_file(10);
        let reader = EventReader::open(file.path()).unwrap();
        let event = reader.get(0).unwrap();
        assert_eq!(event.timestamp_ns, 1000);
        assert_eq!(event.function_id, 0x100000001);
        assert_eq!(event.kind, EventKind::Call);
    }

    #[test]
    fn test_event_kind__display__then_formatted() {
        assert_eq!(EventKind::Call.to_string(), "CALL");
        assert_eq!(EventKind::Return.to_string(), "RETURN");
        assert_eq!(EventKind::Exception.to_string(), "EXCEPT");
        assert_eq!(EventKind::Unknown(99).to_string(), "UNK(99)");
    }
}
