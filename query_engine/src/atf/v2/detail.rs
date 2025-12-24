// User Story: M1_E5_I2 - ATF V2 Detail Reader
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Memory-mapped reader for variable-length detail events

use super::error::{AtfV2Error, Result};
use super::types::{AtfDetailFooter, AtfDetailHeader, DetailEvent};
use memmap2::Mmap;
use std::fs::File;
use std::path::Path;

/// Memory-mapped reader for ATF v2 detail files
pub struct DetailReader {
    mmap: Mmap,
    header: AtfDetailHeader,
    footer: Option<AtfDetailFooter>,
    events_offset: usize,
    /// Index of detail events by sequence for O(1) lookup
    /// event_index[seq] = byte offset in mmap
    event_index: Vec<usize>,
}

impl DetailReader {
    /// Open and memory-map a detail file, building the event index
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
        let header = unsafe { std::ptr::read_unaligned(mmap.as_ptr() as *const AtfDetailHeader) };

        // Validate header
        Self::validate_header(&header)?;

        let events_offset = header.events_offset as usize;

        // Validate events_offset
        if events_offset >= mmap.len() {
            return Err(AtfV2Error::InvalidOffset {
                offset: events_offset,
                file_size: mmap.len(),
            });
        }

        // Try to read footer
        let footer = Self::read_footer(&mmap);

        // Build event index for O(1) lookup
        let event_index = Self::build_event_index(&mmap, &header)?;

        Ok(DetailReader {
            mmap,
            header,
            footer,
            events_offset,
            event_index,
        })
    }

    /// Validate detail header
    fn validate_header(header: &AtfDetailHeader) -> Result<()> {
        // Check magic bytes
        if &header.magic != b"ATD2" {
            return Err(AtfV2Error::InvalidMagic {
                expected: b"ATD2".to_vec(),
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

        Ok(())
    }

    /// Read footer if present
    fn read_footer(mmap: &Mmap) -> Option<AtfDetailFooter> {
        if mmap.len() < 128 {
            return None; // Need at least header + footer
        }

        let footer_offset = mmap.len() - 64;
        let footer = unsafe {
            std::ptr::read_unaligned(
                mmap.as_ptr().add(footer_offset) as *const AtfDetailFooter
            )
        };

        // Validate footer magic
        if &footer.magic == b"2DTA" {
            Some(footer)
        } else {
            None
        }
    }

    /// Build index of detail events for O(1) access
    fn build_event_index(mmap: &Mmap, header: &AtfDetailHeader) -> Result<Vec<usize>> {
        let mut index = Vec::new();
        let mut offset = header.events_offset as usize;
        let end_offset = mmap.len().saturating_sub(64); // Leave room for footer

        while offset + 24 <= end_offset {
            // Record this offset
            index.push(offset);

            // Read total_length
            if offset + 4 > mmap.len() { // LCOV_EXCL_LINE - Defensive: unreachable (offset + 24 <= end_offset implies offset + 4 <= mmap.len())
                break; // LCOV_EXCL_LINE
            }

            let total_length = u32::from_le_bytes([
                mmap[offset],
                mmap[offset + 1],
                mmap[offset + 2],
                mmap[offset + 3],
            ]);

            // Validate total_length
            if total_length < 24 {
                break; // Invalid event
            }

            // Advance to next event
            offset += total_length as usize;
        }

        Ok(index)
    }

    /// Get detail event by sequence number (O(1))
    pub fn get(&self, detail_seq: u32) -> Option<DetailEvent> {
        let offset = *self.event_index.get(detail_seq as usize)?;

        if offset + 24 > self.mmap.len() {
            return None;
        }

        DetailEvent::from_bytes(&self.mmap[offset..])
    }

    /// Get detail event by its linked index sequence (O(n) scan)
    pub fn get_by_index_seq(&self, index_seq: u32) -> Option<DetailEvent> {
        for detail_seq in 0..self.event_index.len() {
            if let Some(event) = self.get(detail_seq as u32) {
                if event.header().index_seq == index_seq {
                    return Some(event);
                }
            }
        }
        None
    }

    /// Get event count
    pub fn len(&self) -> usize {
        self.event_index.len()
    }

    /// Check if empty
    pub fn is_empty(&self) -> bool {
        self.event_index.is_empty()
    }

    /// Get thread ID
    pub fn thread_id(&self) -> u32 {
        self.header.thread_id
    }

    /// Iterate all detail events
    pub fn iter(&self) -> DetailEventIter {
        DetailEventIter {
            reader: self,
            pos: 0,
        }
    }
}

/// Iterator over detail events
pub struct DetailEventIter<'a> {
    reader: &'a DetailReader,
    pos: usize,
}

impl<'a> Iterator for DetailEventIter<'a> {
    type Item = DetailEvent<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let event = self.reader.get(self.pos as u32)?;
        self.pos += 1;
        Some(event)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.reader.event_index.len() - self.pos;
        (remaining, Some(remaining))
    }
}

impl<'a> ExactSizeIterator for DetailEventIter<'a> {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn create_test_detail_file(event_count: u32) -> NamedTempFile {
        let mut file = NamedTempFile::new().unwrap();

        // Calculate total size
        let event_size = 24 + 80; // Header + 80 bytes payload
        let events_size = event_count as usize * event_size;

        // Write header
        let header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: event_count as u64,
            bytes_length: events_size as u64,
            index_seq_start: 0,
            index_seq_end: event_count as u64,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfDetailHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write events
        for i in 0..event_count {
            // Write DetailEventHeader
            let total_length = (24 + 80) as u32;
            file.write_all(&total_length.to_le_bytes()).unwrap();
            file.write_all(&3u16.to_le_bytes()).unwrap(); // event_type
            file.write_all(&0u16.to_le_bytes()).unwrap(); // flags
            file.write_all(&i.to_le_bytes()).unwrap();    // index_seq
            file.write_all(&0u32.to_le_bytes()).unwrap(); // thread_id
            file.write_all(&(1000 + i as u64 * 100).to_le_bytes()).unwrap(); // timestamp

            // Write payload (80 bytes)
            let payload = vec![i as u8; 80];
            file.write_all(&payload).unwrap();
        }

        // Write footer
        let footer = AtfDetailFooter {
            magic: *b"2DTA",
            checksum: 0,
            event_count: event_count as u64,
            bytes_length: events_size as u64,
            time_start_ns: 1000,
            time_end_ns: 1000 + event_count as u64 * 100,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(
                &footer as *const AtfDetailFooter as *const u8,
                64,
            )
        };
        file.write_all(footer_bytes).unwrap();

        file.flush().unwrap();
        file
    }

    #[test]
    fn test_detail_header__valid_magic__then_parsed() {
        // User Story: M1_E5_I2 - Parse valid detail header
        // Test Plan: Unit Tests - Detail Header Parsing
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();
        assert_eq!(&reader.header.magic, b"ATD2");
    }

    #[test]
    fn test_detail_header__invalid_magic__then_error() {
        // User Story: M1_E5_I2 - Reject invalid magic bytes
        // Test Plan: Unit Tests - Detail Header Parsing
        let mut file = NamedTempFile::new().unwrap();
        let mut header_bytes = vec![0u8; 64];
        header_bytes[0..4].copy_from_slice(b"XXXX");
        header_bytes[4] = 0x01; // endian
        header_bytes[5] = 1;    // version
        file.write_all(&header_bytes).unwrap();
        file.flush().unwrap();

        let result = DetailReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::InvalidMagic { .. })));
    }

    #[test]
    fn test_detail_reader__get_by_seq__then_correct() {
        // User Story: M1_E5_I2 - O(1) access to detail events
        // Test Plan: Unit Tests - Detail Event Access
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();
        let event = reader.get(0).unwrap();
        let header = event.header();
        let event_type = header.event_type;
        let index_seq = header.index_seq;
        assert_eq!(event_type, 3);
        assert_eq!(index_seq, 0);
    }

    #[test]
    fn test_detail_reader__variable_length__then_parsed() {
        // User Story: M1_E5_I2 - Parse variable-length events
        // Test Plan: Unit Tests - Detail Event Access
        let file = create_test_detail_file(10);
        let reader = DetailReader::open(file.path()).unwrap();
        for seq in 0..10 {
            let event = reader.get(seq).unwrap();
            assert_eq!(event.payload().len(), 80);
        }
    }

    #[test]
    fn test_detail_reader__out_of_bounds__then_none() {
        // User Story: M1_E5_I2 - Handle out-of-bounds access
        // Test Plan: Unit Tests - Detail Event Access
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();
        let event = reader.get(50);
        assert!(event.is_none());
    }

    #[test]
    fn test_detail_reader__event_index__then_correct_count() {
        // User Story: M1_E5_I2 - Build event index on open
        // Test Plan: Unit Tests - Detail Index Building
        let file = create_test_detail_file(100);
        let reader = DetailReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 100);
        assert_eq!(reader.event_index.len(), 100);
    }

    #[test]
    fn test_detail_reader__iteration__then_sequential() {
        // User Story: M1_E5_I2 - Sequential iteration
        // Test Plan: Unit Tests - Detail Event Access
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();
        let events: Vec<_> = reader.iter().collect();
        assert_eq!(events.len(), 50);

        // Verify index_seq increases
        for (i, event) in events.iter().enumerate() {
            let header = event.header();
            let index_seq = header.index_seq;
            assert_eq!(index_seq, i as u32);
        }
    }

    #[test]
    fn test_detail_reader__get_by_index_seq__then_found() {
        // User Story: M1_E5_I2 - Backward lookup by index_seq
        // Test Plan: Unit Tests - Backward Lookup
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();

        let event = reader.get_by_index_seq(25).unwrap();
        let header = event.header();
        let index_seq = header.index_seq;
        assert_eq!(index_seq, 25);
    }

    #[test]
    fn test_detail_reader__file_too_small__then_error() {
        // User Story: M1_E5_I2 - Handle truncated files
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();
        file.write_all(&[0u8; 32]).unwrap(); // Only 32 bytes
        file.flush().unwrap();

        let result = DetailReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::FileTooSmall { expected: 64, actual: 32 })));
    }

    #[test]
    fn test_detail_reader__invalid_events_offset__then_error() {
        // User Story: M1_E5_I2 - Handle invalid events_offset
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let mut header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 10000, // Beyond file size
            event_count: 0,
            bytes_length: 0,
            index_seq_start: 0,
            index_seq_end: 0,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfDetailHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = DetailReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::InvalidOffset { .. })));
    }

    #[test]
    fn test_detail_reader__unsupported_version__then_error() {
        // User Story: M1_E5_I2 - Reject unsupported version
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let mut header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 99, // Unsupported version
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: 0,
            bytes_length: 0,
            index_seq_start: 0,
            index_seq_end: 0,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfDetailHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = DetailReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::UnsupportedVersion(99))));
    }

    #[test]
    fn test_detail_reader__unsupported_endian__then_error() {
        // User Story: M1_E5_I2 - Reject unsupported endianness
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let mut header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x00, // Big endian (unsupported)
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: 0,
            bytes_length: 0,
            index_seq_start: 0,
            index_seq_end: 0,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &mut header as *mut AtfDetailHeader as *mut u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();
        file.flush().unwrap();

        let result = DetailReader::open(file.path());
        assert!(matches!(result, Err(AtfV2Error::UnsupportedEndian(0x00))));
    }

    #[test]
    fn test_detail_reader__corrupted_event_length__then_stops_indexing() {
        // User Story: M1_E5_I2 - Handle corrupted event data (line 134 coverage)
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        // Write valid header
        let header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: 2,
            bytes_length: 200,
            index_seq_start: 0,
            index_seq_end: 2,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfDetailHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write one valid event
        let total_length = 104u32; // 24 + 80
        file.write_all(&total_length.to_le_bytes()).unwrap();
        file.write_all(&3u16.to_le_bytes()).unwrap(); // event_type
        file.write_all(&0u16.to_le_bytes()).unwrap(); // flags
        file.write_all(&0u32.to_le_bytes()).unwrap(); // index_seq
        file.write_all(&0u32.to_le_bytes()).unwrap(); // thread_id
        file.write_all(&1000u64.to_le_bytes()).unwrap(); // timestamp
        file.write_all(&vec![0u8; 80]).unwrap(); // payload

        // Write corrupted event with total_length < 24
        let corrupted_length = 10u32; // Invalid: less than minimum 24 bytes
        file.write_all(&corrupted_length.to_le_bytes()).unwrap();
        file.write_all(&vec![0u8; 20]).unwrap(); // Some junk data

        // Add padding to make file large enough so while loop enters
        // File currently: 64 + 104 + 4 + 20 = 192
        // end_offset = file_size - 64
        // After event1, offset = 168
        // Need: 168 + 24 <= end_offset, so end_offset >= 192
        // So file_size >= 256
        file.write_all(&vec![0u8; 64]).unwrap(); // Add 64 bytes padding

        file.flush().unwrap();

        // Reader pushes offset before validating, then breaks on invalid total_length
        let reader = DetailReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 2); // Indexed both offsets, but second is corrupted

        // First event should be readable
        assert!(reader.get(0).is_some());

        // Second event should fail to parse due to corrupted length
        assert!(reader.get(1).is_none());
    }

    #[test]
    fn test_detail_reader__no_footer__then_returns_none() {
        // User Story: M1_E5_I2 - Handle file without footer (64-127 bytes)
        // Test Plan: Unit Tests - Detail Footer Handling
        let mut file = NamedTempFile::new().unwrap();

        // Write header
        let header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 42,
            _reserved1: 0,
            events_offset: 64,
            event_count: 0,
            bytes_length: 0,
            index_seq_start: 0,
            index_seq_end: 0,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfDetailHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write 32 more bytes (total 96, less than 128 needed for footer)
        file.write_all(&[0u8; 32]).unwrap();
        file.flush().unwrap();

        let reader = DetailReader::open(file.path()).unwrap();
        assert!(reader.footer.is_none());
    }

    #[test]
    fn test_detail_reader__truncated_during_indexing__then_stops_at_boundary() {
        // User Story: M1_E5_I2 - Handle truncated file during indexing
        // Test Plan: Error Handling Tests
        let mut file = NamedTempFile::new().unwrap();

        let header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: 2,
            bytes_length: 200,
            index_seq_start: 0,
            index_seq_end: 2,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfDetailHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write one valid event
        let total_length = 104u32;
        file.write_all(&total_length.to_le_bytes()).unwrap();
        file.write_all(&3u16.to_le_bytes()).unwrap();
        file.write_all(&0u16.to_le_bytes()).unwrap();
        file.write_all(&0u32.to_le_bytes()).unwrap();
        file.write_all(&0u32.to_le_bytes()).unwrap();
        file.write_all(&1000u64.to_le_bytes()).unwrap();
        file.write_all(&vec![0u8; 80]).unwrap();

        // Write partial second event (only length field, then truncate)
        file.write_all(&104u32.to_le_bytes()).unwrap();
        // No more data - file ends abruptly
        file.flush().unwrap();

        let reader = DetailReader::open(file.path()).unwrap();
        assert_eq!(reader.len(), 1); // Only indexed the complete event
    }

    #[test]
    fn test_detail_reader__empty_detail_file__then_is_empty_true() {
        // User Story: M1_E5_I2 - Handle empty detail file
        // Test Plan: Unit Tests - Detail Event Access
        let file = create_test_detail_file(0);
        let reader = DetailReader::open(file.path()).unwrap();
        assert!(reader.is_empty());
        assert_eq!(reader.len(), 0);
    }

    #[test]
    fn test_detail_reader__thread_id__then_returns_header_value() {
        // User Story: M1_E5_I2 - Retrieve thread_id from header
        // Test Plan: Unit Tests - Detail Reader Getters
        let file = create_test_detail_file(10);
        let reader = DetailReader::open(file.path()).unwrap();
        assert_eq!(reader.thread_id(), 0);
    }

    #[test]
    fn test_detail_reader__no_match_in_get_by_index_seq__then_none() {
        // User Story: M1_E5_I2 - Handle non-existent index_seq lookup
        // Test Plan: Unit Tests - Backward Lookup
        let file = create_test_detail_file(50);
        let reader = DetailReader::open(file.path()).unwrap();

        // Try to get an index_seq that doesn't exist
        let result = reader.get_by_index_seq(9999);
        assert!(result.is_none());
    }

    #[test]
    fn test_detail_reader__get_out_of_bounds_offset__then_none() {
        // User Story: M1_E5_I2 - Handle offset beyond file size
        // Test Plan: Unit Tests - Detail Event Access
        let mut file = NamedTempFile::new().unwrap();

        let header = AtfDetailHeader {
            magic: *b"ATD2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: 0,
            thread_id: 0,
            _reserved1: 0,
            events_offset: 64,
            event_count: 1,
            bytes_length: 104,
            index_seq_start: 0,
            index_seq_end: 1,
            _reserved2: [0; 4],
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const AtfDetailHeader as *const u8,
                64,
            )
        };
        file.write_all(header_bytes).unwrap();

        // Write one event
        let total_length = 104u32;
        file.write_all(&total_length.to_le_bytes()).unwrap();
        file.write_all(&3u16.to_le_bytes()).unwrap();
        file.write_all(&0u16.to_le_bytes()).unwrap();
        file.write_all(&0u32.to_le_bytes()).unwrap();
        file.write_all(&0u32.to_le_bytes()).unwrap();
        file.write_all(&1000u64.to_le_bytes()).unwrap();
        file.write_all(&vec![0u8; 80]).unwrap();
        file.flush().unwrap();

        let mut reader = DetailReader::open(file.path()).unwrap();

        // Manually corrupt the event_index to point beyond file
        reader.event_index.push(10000);

        // This should return None because offset + 24 > mmap.len()
        let result = reader.get(1);
        assert!(result.is_none());
    }
}
