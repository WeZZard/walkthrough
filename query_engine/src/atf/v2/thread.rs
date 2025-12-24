// User Story: M1_E5_I2 - ATF V2 Thread Reader
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Combined reader for bidirectional navigation

use super::detail::DetailReader;
use super::error::Result;
use super::index::IndexReader;
use super::types::{DetailEvent, IndexEvent, ATF_NO_DETAIL_SEQ};
use std::path::Path;

/// Combined reader for thread index + detail files
pub struct ThreadReader {
    pub index: IndexReader,
    pub detail: Option<DetailReader>,
}

impl ThreadReader {
    /// Open thread directory and load index + optional detail files
    pub fn open(thread_dir: &Path) -> Result<Self> {
        let index_path = thread_dir.join("index.atf");
        let index = IndexReader::open(&index_path)?;

        // Try to open detail file if it exists
        let detail_path = thread_dir.join("detail.atf");
        let detail = if detail_path.exists() {
            Some(DetailReader::open(&detail_path)?)
        } else {
            None
        };

        Ok(ThreadReader { index, detail })
    }

    /// Forward lookup: index event → paired detail event (O(1))
    pub fn get_detail_for(&self, index_event: &IndexEvent) -> Option<DetailEvent> {
        if index_event.detail_seq == ATF_NO_DETAIL_SEQ {
            return None;
        }

        self.detail.as_ref()?.get(index_event.detail_seq)
    }

    /// Backward lookup: detail event → paired index event (O(1))
    pub fn get_index_for(&self, detail_event: &DetailEvent) -> Option<&IndexEvent> {
        self.index.get(detail_event.header().index_seq)
    }

    /// Get thread ID
    pub fn thread_id(&self) -> u32 {
        self.index.thread_id()
    }

    /// Get time range
    pub fn time_range(&self) -> (u64, u64) {
        self.index.time_range()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_test_thread_dir(event_count: u32, with_detail: bool) -> TempDir {
        let dir = TempDir::new().unwrap();

        // Create index file
        let index_path = dir.path().join("index.atf");
        let mut index_file = fs::File::create(&index_path).unwrap();

        use super::super::types::AtfIndexHeader;

        let header = AtfIndexHeader {
            magic: *b"ATI2",
            endian: 0x01,
            version: 1,
            arch: 1,
            os: 3,
            flags: if with_detail { 1 } else { 0 },
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
        index_file.write_all(header_bytes).unwrap();

        // Write index events
        for i in 0..event_count {
            let event = IndexEvent {
                timestamp_ns: 1000 + i as u64 * 100,
                function_id: 0x100000001 + i as u64,
                thread_id: 0,
                event_kind: if i % 2 == 0 { 1 } else { 2 },
                call_depth: i % 10,
                detail_seq: if with_detail { i } else { ATF_NO_DETAIL_SEQ },
            };

            let event_bytes = unsafe {
                std::slice::from_raw_parts(
                    &event as *const IndexEvent as *const u8,
                    32,
                )
            };
            index_file.write_all(event_bytes).unwrap();
        }

        // Write footer
        use super::super::types::AtfIndexFooter;
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
        index_file.write_all(footer_bytes).unwrap();
        index_file.flush().unwrap();

        // Create detail file if requested
        if with_detail {
            let detail_path = dir.path().join("detail.atf");
            let mut detail_file = fs::File::create(&detail_path).unwrap();

            use super::super::types::AtfDetailHeader;

            let event_size = 24 + 80;
            let events_size = event_count as usize * event_size;

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
            detail_file.write_all(header_bytes).unwrap();

            // Write detail events
            for i in 0..event_count {
                let total_length = (24 + 80) as u32;
                detail_file.write_all(&total_length.to_le_bytes()).unwrap();
                detail_file.write_all(&3u16.to_le_bytes()).unwrap(); // event_type
                detail_file.write_all(&0u16.to_le_bytes()).unwrap(); // flags
                detail_file.write_all(&i.to_le_bytes()).unwrap();    // index_seq
                detail_file.write_all(&0u32.to_le_bytes()).unwrap(); // thread_id
                detail_file.write_all(&(1000 + i as u64 * 100).to_le_bytes()).unwrap(); // timestamp

                // Write payload
                let payload = vec![i as u8; 80];
                detail_file.write_all(&payload).unwrap();
            }

            // Write footer
            use super::super::types::AtfDetailFooter;
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
            detail_file.write_all(footer_bytes).unwrap();
            detail_file.flush().unwrap();
        }

        dir
    }

    #[test]
    fn test_thread_reader__opens_index_only__then_no_detail() {
        // User Story: M1_E5_I2 - Open thread with index file only
        // Test Plan: Integration Tests - Index-Only Session
        let dir = create_test_thread_dir(100, false);
        let thread = ThreadReader::open(dir.path()).unwrap();

        assert!(thread.detail.is_none());
        assert_eq!(thread.index.len(), 100);
    }

    #[test]
    fn test_thread_reader__opens_with_detail__then_both_loaded() {
        // User Story: M1_E5_I2 - Open thread with both files
        // Test Plan: Integration Tests - Bidirectional Links Valid
        let dir = create_test_thread_dir(100, true);
        let thread = ThreadReader::open(dir.path()).unwrap();

        assert!(thread.detail.is_some());
        assert_eq!(thread.index.len(), 100);
        assert_eq!(thread.detail.as_ref().unwrap().len(), 100);
    }

    #[test]
    fn test_forward_lookup__has_detail__then_returns_event() {
        // User Story: M1_E5_I2 - Forward navigation from index to detail
        // Test Plan: Unit Tests - Bidirectional Navigation
        let dir = create_test_thread_dir(100, true);
        let thread = ThreadReader::open(dir.path()).unwrap();

        let index_event = thread.index.get(10).unwrap();
        let detail_seq = index_event.detail_seq;
        assert_ne!(detail_seq, ATF_NO_DETAIL_SEQ);

        let detail = thread.get_detail_for(index_event);
        assert!(detail.is_some());
        let header = detail.unwrap().header();
        let index_seq = header.index_seq;
        assert_eq!(index_seq, 10);
    }

    #[test]
    fn test_forward_lookup__no_detail__then_none() {
        // User Story: M1_E5_I2 - Handle missing detail file
        // Test Plan: Unit Tests - Bidirectional Navigation
        let dir = create_test_thread_dir(100, false);
        let thread = ThreadReader::open(dir.path()).unwrap();

        let index_event = thread.index.get(10).unwrap();
        let detail_seq = index_event.detail_seq;
        assert_eq!(detail_seq, ATF_NO_DETAIL_SEQ);

        let detail = thread.get_detail_for(index_event);
        assert!(detail.is_none());
    }

    #[test]
    fn test_backward_lookup__valid_index_seq__then_returns_event() {
        // User Story: M1_E5_I2 - Backward navigation from detail to index
        // Test Plan: Unit Tests - Bidirectional Navigation
        let dir = create_test_thread_dir(100, true);
        let thread = ThreadReader::open(dir.path()).unwrap();

        let detail_event = thread.detail.as_ref().unwrap().get(5).unwrap();

        let index = thread.get_index_for(&detail_event);
        assert!(index.is_some());
        let detail_seq = index.unwrap().detail_seq;
        assert_eq!(detail_seq, 5);
    }

    #[test]
    fn test_bidirectional__round_trip__then_consistent() {
        // User Story: M1_E5_I2 - Verify bidirectional consistency
        // Test Plan: Unit Tests - Bidirectional Navigation
        let dir = create_test_thread_dir(100, true);
        let thread = ThreadReader::open(dir.path()).unwrap();

        // For each index event with detail, verify round-trip
        for index_event in thread.index.iter() {
            let detail_seq = index_event.detail_seq;
            if detail_seq == ATF_NO_DETAIL_SEQ {
                continue; // LCOV_EXCL_LINE - Test skip path
            }

            // Forward: index → detail
            let detail = thread.get_detail_for(index_event).unwrap();

            // Backward: detail → index
            let back_to_index = thread.get_index_for(&detail).unwrap();

            // Should be same event (copy values to avoid packed field issues)
            let back_timestamp = back_to_index.timestamp_ns;
            let back_function_id = back_to_index.function_id;
            let orig_timestamp = index_event.timestamp_ns;
            let orig_function_id = index_event.function_id;

            assert_eq!(back_timestamp, orig_timestamp);
            assert_eq!(back_function_id, orig_function_id);
        }
    }

    #[test]
    fn test_thread_reader__getters__then_return_index_values() {
        // User Story: M1_E5_I2 - Access thread_id and time_range
        // Test Plan: Unit Tests - Thread Reader Getters
        let dir = create_test_thread_dir(100, false);
        let thread = ThreadReader::open(dir.path()).unwrap();

        assert_eq!(thread.thread_id(), 0);

        let (start, end) = thread.time_range();
        assert_eq!(start, 1000);
        assert_eq!(end, 1000 + 100 * 100);
    }
}
