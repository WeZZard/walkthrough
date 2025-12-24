// User Story: M1_E5_I2 - ATF V2 Index Reader
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Memory-mapped reader for index files with O(1) access

use super::error::{AtfV2Error, Result};
use super::types::{AtfIndexFooter, AtfIndexHeader, IndexEvent, ATF_INDEX_FLAG_HAS_DETAIL_FILE};
use memmap2::Mmap;
use std::fs::File;
use std::path::Path;

/// Memory-mapped reader for ATF v2 index files
pub struct IndexReader {
    _mmap: Mmap,
    header: AtfIndexHeader,
    footer: Option<AtfIndexFooter>,
    events_offset: usize,
    event_count: u32,
    events_ptr: *const IndexEvent,
}

// SAFETY: IndexReader is Send + Sync because:
// - Mmap is Send + Sync
// - events_ptr points to memory-mapped read-only data
// - No interior mutability
unsafe impl Send for IndexReader {}
unsafe impl Sync for IndexReader {}

impl IndexReader {
    /// Open and memory-map an index file
    pub fn open(path: &Path) -> Result<Self> {
        let file = File::open(path)?;
        let mmap = unsafe { Mmap::map(&file)? };

        // Validate file size (at least header)
        if mmap.len() < 64 {
            return Err(AtfV2Error::FileTooSmall {
                expected: 64,
                actual: mmap.len(),
            });
        }

        // Parse header
        let header = unsafe { std::ptr::read_unaligned(mmap.as_ptr() as *const AtfIndexHeader) };

        // Validate header
        Self::validate_header(&header)?;

        // Try to read footer (authoritative for event count)
        let (footer, event_count) = Self::read_footer(&mmap, &header);

        let events_offset = header.events_offset as usize;

        // Validate events_offset
        if events_offset >= mmap.len() {
            return Err(AtfV2Error::InvalidOffset {
                offset: events_offset,
                file_size: mmap.len(),
            });
        }

        // Calculate pointer to events array
        let events_ptr = unsafe { mmap.as_ptr().add(events_offset) as *const IndexEvent };

        Ok(IndexReader {
            _mmap: mmap,
            header,
            footer,
            events_offset,
            event_count,
            events_ptr,
        })
    }

    /// Validate index header
    fn validate_header(header: &AtfIndexHeader) -> Result<()> {
        // Check magic bytes
        if &header.magic != b"ATI2" {
            return Err(AtfV2Error::InvalidMagic {
                expected: b"ATI2".to_vec(),
                got: header.magic.to_vec(),
            });
        }

        // Check version
        if header.version != 1 {
            return Err(AtfV2Error::UnsupportedVersion(header.version));
        }

        // Check endianness (only little-endian supported)
        if header.endian != 0x01 {
            return Err(AtfV2Error::UnsupportedEndian(header.endian));
        }

        // Check event size
        if header.event_size != 32 {
            return Err(AtfV2Error::InvalidEventSize(header.event_size));
        }

        Ok(())
    }

    /// Read footer and determine authoritative event count
    fn read_footer(mmap: &Mmap, header: &AtfIndexHeader) -> (Option<AtfIndexFooter>, u32) {
        let footer_offset = header.footer_offset as usize;

        // Try to read footer
        if footer_offset + 64 <= mmap.len() {
            let footer = unsafe {
                std::ptr::read_unaligned(
                    mmap.as_ptr().add(footer_offset) as *const AtfIndexFooter
                )
            };

            // Validate footer magic
            if &footer.magic == b"2ITA" {
                // Footer is valid, use its count (authoritative)
                return (Some(footer), footer.event_count as u32);
            }
        }

        // Footer invalid or missing, calculate from file size
        let events_section_size = mmap.len().saturating_sub(header.events_offset as usize);
        let calculated_count = (events_section_size / 32) as u32;

        (None, calculated_count)
    }

    /// Get event by sequence number (O(1))
    pub fn get(&self, seq: u32) -> Option<&IndexEvent> {
        if seq >= self.event_count {
            return None;
        }

        // SAFETY: We validated seq < event_count, and events_ptr points to valid mmap memory
        unsafe { Some(&*self.events_ptr.add(seq as usize)) }
    }

    /// Get event count
    pub fn len(&self) -> u32 {
        self.event_count
    }

    /// Check if empty
    pub fn is_empty(&self) -> bool {
        self.event_count == 0
    }

    /// Check if detail file exists
    pub fn has_detail(&self) -> bool {
        (self.header.flags & ATF_INDEX_FLAG_HAS_DETAIL_FILE) != 0
    }

    /// Get time range
    pub fn time_range(&self) -> (u64, u64) {
        // Use footer if available (more reliable), otherwise header
        if let Some(footer) = &self.footer {
            (footer.time_start_ns, footer.time_end_ns)
        } else {
            (self.header.time_start_ns, self.header.time_end_ns)
        }
    }

    /// Get thread ID
    pub fn thread_id(&self) -> u32 {
        self.header.thread_id
    }

    /// Iterate all events
    pub fn iter(&self) -> IndexEventIter {
        IndexEventIter {
            reader: self,
            pos: 0,
        }
    }
}

/// Iterator over index events
pub struct IndexEventIter<'a> {
    reader: &'a IndexReader,
    pos: u32,
}

impl<'a> Iterator for IndexEventIter<'a> {
    type Item = &'a IndexEvent;

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

impl<'a> ExactSizeIterator for IndexEventIter<'a> {}

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
            thread_id: 0,
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
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write events
        for i in 0..event_count {
            let event = IndexEvent {
                timestamp_ns: 1000 + i as u64 * 100,
                function_id: 0x100000001 + i as u64,
                thread_id: 0,
                event_kind: if i % 2 == 0 { 1 } else { 2 },
                call_depth: i % 10,
                detail_seq: u32::MAX,
            };

            let event_bytes = unsafe {
                std::slice::from_raw_parts(
                    &event as *const IndexEvent as *const u8,
                    32,
                )
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
            std::slice::from_raw_parts(
                &footer as *const AtfIndexFooter as *const u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();

        file.flush().unwrap();
        file
    }

    #[test]
    fn test_index_header__valid_magic__then_parsed() {
        // User Story: M1_E5_I2 - Parse valid index header
        // Test Plan: Unit Tests - Index Header Parsing
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        assert_eq!(&reader.header.magic, b"ATI2");
    }

    #[test]
    fn test_index_header__invalid_magic__then_error() {
        // User Story: M1_E5_I2 - Reject invalid magic bytes
        // Test Plan: Unit Tests - Index Header Parsing
        let mut file = NamedTempFile::new().unwrap();
        let mut header_bytes = vec![0u8; 64];
        header_bytes[0..4].copy_from_slice(b"XXXX");
        header_bytes[4] = 0x01; // endian
        header_bytes[5] = 1;    // version
        file.write_all(&header_bytes).unwrap();
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::InvalidMagic { .. })));
    }

    #[test]
    fn test_index_header__wrong_version__then_error() {
        // User Story: M1_E5_I2 - Reject unsupported version
        // Test Plan: Unit Tests - Index Header Parsing
        let mut file = NamedTempFile::new().unwrap();
        let mut header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 99,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 0,
            events_offset: 64,
            footer_offset: 64,
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfIndexHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::UnsupportedVersion(99))));
    }

    #[test]
    fn test_index_header__big_endian__then_error() {
        // User Story: M1_E5_I2 - Reject big-endian files
        // Test Plan: Unit Tests - Index Header Parsing
        let mut file = NamedTempFile::new().unwrap();
        let mut header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x00, // Big endian
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 0,
            events_offset: 64,
            footer_offset: 64,
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfIndexHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::UnsupportedEndian(0x00))));
    }

    #[test]
    fn test_index_reader__get_first_event__then_correct() {
        // User Story: M1_E5_I2 - O(1) access to index events
        // Test Plan: Unit Tests - Index Event Access
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        let event = reader.get(0).unwrap();
        // Copy values to avoid packed struct alignment issues
        let timestamp = event.timestamp_ns;
        let function_id = event.function_id;
        assert_eq!(timestamp, 1000);
        assert_eq!(function_id, 0x100000001);
    }

    #[test]
    fn test_index_reader__get_last_event__then_correct() {
        // User Story: M1_E5_I2 - O(1) access to any event
        // Test Plan: Unit Tests - Index Event Access
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        let event = reader.get(99).unwrap();
        let timestamp = event.timestamp_ns;
        assert_eq!(timestamp, 1000 + 99 * 100);
    }

    #[test]
    fn test_index_reader__out_of_bounds__then_none() {
        // User Story: M1_E5_I2 - Handle out-of-bounds access
        // Test Plan: Unit Tests - Index Event Access
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        let event = reader.get(100);
        assert!(event.is_none());
    }

    #[test]
    fn test_index_reader__iteration__then_sequential() {
        // User Story: M1_E5_I2 - Sequential iteration
        // Test Plan: Unit Tests - Index Event Access
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        let events: Vec<_> = reader.iter().collect();
        assert_eq!(events.len(), 100);

        // Verify timestamps are non-decreasing
        for window in events.windows(2) {
            assert!(window[0].timestamp_ns <= window[1].timestamp_ns);
        }
    }

    #[test]
    fn test_index_reader__valid_footer__then_uses_footer_count() {
        // User Story: M1_E5_I2 - Footer-based crash recovery
        // Test Plan: Unit Tests - Index Footer Recovery
        let file = create_test_index_file(100);
        let reader = IndexReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 100);
        assert!(reader.footer.is_some());
    }

    #[test]
    fn test_index_reader__file_too_small__then_error() {
        // User Story: M1_E5_I2 - Handle truncated files
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();
        file.write_all(&[0u8; 32]).unwrap(); // Only 32 bytes
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::FileTooSmall { .. })));
    }

    #[test]
    fn test_index_reader__invalid_events_offset__then_error() {
        // User Story: M1_E5_I2 - Handle invalid events_offset
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let mut header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 0,
            events_offset: 10000, // Beyond file size
            footer_offset: 64,
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfIndexHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::InvalidOffset { .. })));
    }

    #[test]
    fn test_index_reader__invalid_event_size__then_error() {
        // User Story: M1_E5_I2 - Reject invalid event_size
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let mut header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 64, // Invalid: must be 32
            event_count: 0,
            events_offset: 64,
            footer_offset: 64,
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfIndexHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = IndexReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::InvalidEventSize(64))));
    }

    #[test]
    fn test_index_reader__empty_file__then_iterator_works() {
        // User Story: M1_E5_I2 - Handle empty index files
        // Test Plan: Unit Tests - Index Event Access
        let file = create_test_index_file(0);
        let reader = IndexReader::open(file.path()).unwrap();

        assert_eq!(reader.len(), 0);
        assert!(reader.is_empty());

        let events: Vec<_> = reader.iter().collect();
        assert_eq!(events.len(), 0);
    }

    #[test]
    fn test_index_reader__has_detail_flag_set__then_has_detail_true() {
        // User Story: M1_E5_I2 - Check has_detail flag
        // Test Plan: Unit Tests - Index Header Parsing
        let mut file = NamedTempFile::new().unwrap();

        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: ATF_INDEX_FLAG_HAS_DETAIL_FILE,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 0,
            events_offset: 64,
            footer_offset: 128,
            time_start_ns: 1000,
            time_end_ns: 2000,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write footer
        let footer = AtfIndexFooter {
            magic: *b"2ITA",
            checksum: 0,
            event_count: 0,
            time_start_ns: 1000,
            time_end_ns: 2000,
            bytes_written: 0,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(
                &footer as *const AtfIndexFooter as *const u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();
        file.flush().unwrap();

        let reader = IndexReader::open(file.path()).unwrap();
        assert!(reader.has_detail());
    }

    #[test]
    fn test_index_reader__time_range_no_footer__then_uses_header() {
        // User Story: M1_E5_I2 - Fall back to header time range when no footer
        // Test Plan: Unit Tests - Index Footer Recovery
        let mut file = NamedTempFile::new().unwrap();

        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 0,
            events_offset: 64,
            footer_offset: 10000, // Invalid footer offset
            time_start_ns: 1000,
            time_end_ns: 2000,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write some padding to make file valid (events_offset must be < file_size)
        file.write_all(&[0u8; 32]).unwrap();
        file.flush().unwrap();

        let reader = IndexReader::open(file.path()).unwrap();
        let (start, end) = reader.time_range();
        assert_eq!(start, 1000);
        assert_eq!(end, 2000);
    }

    #[test]
    fn test_index_reader__thread_id__then_returns_header_value() {
        // User Story: M1_E5_I2 - Retrieve thread_id from header
        // Test Plan: Unit Tests - Index Reader Getters
        let mut file = NamedTempFile::new().unwrap();

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
            event_count: 0,
            events_offset: 64,
            footer_offset: 128,
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write footer
        let footer = AtfIndexFooter {
            magic: *b"2ITA",
            checksum: 0,
            event_count: 0,
            time_start_ns: 0,
            time_end_ns: 0,
            bytes_written: 0,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(
                &footer as *const AtfIndexFooter as *const u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();
        file.flush().unwrap();

        let reader = IndexReader::open(file.path()).unwrap();
        assert_eq!(reader.thread_id(), 42);
    }

    #[test]
    fn test_index_reader__valid_footer__then_returns_footer_count() {
        // User Story: M1_E5_I2 - Use footer count when available
        // Test Plan: Unit Tests - Index Footer Recovery
        let mut file = NamedTempFile::new().unwrap();

        // Header says 5 events, but footer says 3 (footer is authoritative)
        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 5,
            events_offset: 64,
            footer_offset: 64 + 32 * 3, // Footer after 3 events
            time_start_ns: 1000,
            time_end_ns: 2000,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write 3 events
        for i in 0..3 {
            let event = IndexEvent {
                timestamp_ns: 1000 + i * 100,
                function_id: 0x100000001,
                thread_id: 0,
                event_kind: 1,
                call_depth: 0,
                detail_seq: u32::MAX,
            };

            let event_bytes = unsafe {
                std::slice::from_raw_parts(
                    &event as *const IndexEvent as *const u8,
                    32,
                )
            };
            file.write_all(event_bytes).unwrap();
        }

        // Write footer with correct count
        let footer = AtfIndexFooter {
            magic: *b"2ITA",
            checksum: 0,
            event_count: 3,
            time_start_ns: 1000,
            time_end_ns: 1300,
            bytes_written: 96,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(
                &footer as *const AtfIndexFooter as *const u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();
        file.flush().unwrap();

        let reader = IndexReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 3); // Footer count is authoritative
        assert!(reader.footer.is_some());
    }

    #[test]
    fn test_index_reader__invalid_footer_magic__then_uses_calculated_count() {
        // User Story: M1_E5_I2 - Fallback when footer magic is invalid
        // Test Plan: Unit Tests - Index Footer Recovery
        let mut file = NamedTempFile::new().unwrap();

        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            clock_type: 1,
            _reserved1: [0; 3],
            _reserved2: 0,
            event_size: 32,
            event_count: 999, // Wrong count
            events_offset: 64,
            footer_offset: 64 + 32 * 3,
            time_start_ns: 1000,
            time_end_ns: 2000,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfIndexHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write 3 events
        for i in 0..3 {
            let event = IndexEvent {
                timestamp_ns: 1000 + i * 100,
                function_id: 0x100000001,
                thread_id: 0,
                event_kind: 1,
                call_depth: 0,
                detail_seq: u32::MAX,
            };

            let event_bytes = unsafe {
                std::slice::from_raw_parts(
                    &event as *const IndexEvent as *const u8,
                    32,
                )
            };
            file.write_all(event_bytes).unwrap();
        }

        // Write footer with INVALID magic
        let mut footer = AtfIndexFooter {
            magic: *b"XXXX", // Invalid magic
            checksum: 0,
            event_count: 3,
            time_start_ns: 1000,
            time_end_ns: 1300,
            bytes_written: 96,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut footer as *mut AtfIndexFooter as *mut u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();
        file.flush().unwrap();

        let reader = IndexReader::open(file.path()).unwrap();
        // With invalid footer magic, reader calculates count from file size
        // File has: header (64) + 3 events (96) + footer (64) = 224 bytes
        // Events section = 224 - 64 = 160 bytes
        // Count = 160 / 32 = 5 events (includes invalid footer as "events")
        assert_eq!(reader.len(), 5); // Calculated from file size
        assert!(reader.footer.is_none()); // Footer should be None due to invalid magic
    }
}
