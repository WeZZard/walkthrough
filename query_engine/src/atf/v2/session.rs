// User Story: M1_E5_I2 - ATF V2 Session Reader
// Tech Spec: M1_E5_I2_TECH_DESIGN.md - Cross-thread merge-sort iterator

use super::error::Result;
use super::thread::ThreadReader;
use super::types::IndexEvent;
use serde::{Deserialize, Serialize};
use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::fs;
use std::path::Path;

/// Manifest describing the session
#[derive(Debug, Serialize, Deserialize)]
pub struct Manifest {
    pub threads: Vec<ThreadInfo>,
    #[serde(default)]
    pub time_start_ns: u64,
    #[serde(default)]
    pub time_end_ns: u64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ThreadInfo {
    pub id: u32,
    #[serde(default)]
    pub has_detail: bool,
}

/// Session reader with multi-thread support
pub struct SessionReader {
    manifest: Manifest,
    threads: Vec<ThreadReader>,
}

impl SessionReader {
    /// Open session directory and load all thread readers
    pub fn open(session_dir: &Path) -> Result<Self> {
        // Read manifest.json
        let manifest_path = session_dir.join("manifest.json");
        let manifest_str = fs::read_to_string(&manifest_path)?;
        let manifest: Manifest = serde_json::from_str(&manifest_str)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;

        // Load thread readers
        let mut threads = Vec::new();
        for thread_info in &manifest.threads {
            let thread_dir = session_dir.join(format!("thread_{}", thread_info.id));
            if thread_dir.exists() {
                threads.push(ThreadReader::open(&thread_dir)?);
            }
        }

        Ok(SessionReader { manifest, threads })
    }

    /// Get all thread readers
    pub fn threads(&self) -> &[ThreadReader] {
        &self.threads
    }

    /// Get manifest
    pub fn manifest(&self) -> &Manifest {
        &self.manifest
    }

    /// Time range across all threads
    pub fn time_range(&self) -> (u64, u64) {
        if self.threads.is_empty() {
            return (0, 0);
        }

        let mut min_time = u64::MAX;
        let mut max_time = 0u64;

        for thread in &self.threads {
            let (start, end) = thread.time_range();
            min_time = min_time.min(start);
            max_time = max_time.max(end);
        }

        (min_time, max_time)
    }

    /// Total event count across all threads
    pub fn event_count(&self) -> u64 {
        self.threads.iter().map(|t| t.index.len() as u64).sum()
    }

    /// Merge-sort iterator across all threads by timestamp_ns
    pub fn merged_iter(&self) -> MergedEventIter {
        MergedEventIter::new(&self.threads)
    }
}

/// Merge-sort iterator using min-heap
pub struct MergedEventIter<'a> {
    heap: BinaryHeap<Reverse<(u64, usize, u32)>>, // (timestamp, thread_idx, seq)
    threads: &'a [ThreadReader],
}

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

impl<'a> Iterator for MergedEventIter<'a> {
    type Item = (usize, &'a IndexEvent); // (thread_idx, event)

    fn next(&mut self) -> Option<Self::Item> {
        // Pop smallest timestamp from heap
        let Reverse((_, thread_idx, seq)) = self.heap.pop()?;

        let event = self.threads[thread_idx].index.get(seq)?;

        // Push next event from same thread if available
        if let Some(next_event) = self.threads[thread_idx].index.get(seq + 1) {
            self.heap
                .push(Reverse((next_event.timestamp_ns, thread_idx, seq + 1)));
        }

        Some((thread_idx, event))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_test_session(thread_count: usize, events_per_thread: u32) -> TempDir {
        let dir = TempDir::new().unwrap();

        // Create manifest
        let mut thread_infos = Vec::new();
        for i in 0..thread_count {
            thread_infos.push(ThreadInfo {
                id: i as u32,
                has_detail: false,
            });
        }

        let manifest = Manifest {
            threads: thread_infos,
            time_start_ns: 1000,
            time_end_ns: 1000 + events_per_thread as u64 * 100 * thread_count as u64,
        };

        let manifest_str = serde_json::to_string_pretty(&manifest).unwrap();
        fs::write(dir.path().join("manifest.json"), manifest_str).unwrap();

        // Create thread directories
        for thread_id in 0..thread_count {
            let thread_dir = dir.path().join(format!("thread_{}", thread_id));
            fs::create_dir(&thread_dir).unwrap();

            // Create index file
            let index_path = thread_dir.join("index.atf");
            let mut index_file = fs::File::create(&index_path).unwrap();

            use super::super::types::{AtfIndexFooter, AtfIndexHeader, IndexEvent};

            let header = AtfIndexHeader {
                magic: *b"ATI2",
                endian: 0x01,
                version: 1,
                arch: 1,
                os: 3,
                flags: 0,
                thread_id: thread_id as u32,
                clock_type: 1,
                _reserved1: [0; 3],
                _reserved2: 0,
                event_size: 32,
                event_count: events_per_thread,
                events_offset: 64,
                footer_offset: 64 + events_per_thread as u64 * 32,
                time_start_ns: 1000 + thread_id as u64 * 50,
                time_end_ns: 1000 + thread_id as u64 * 50 + events_per_thread as u64 * 100,
            };

            let header_bytes = unsafe {
                std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
            };
            index_file.write_all(header_bytes).unwrap();

            // Write events with interleaved timestamps
            for i in 0..events_per_thread {
                let event = IndexEvent {
                    timestamp_ns: 1000 + thread_id as u64 * 50 + i as u64 * 100,
                    function_id: 0x100000001 + i as u64,
                    thread_id: thread_id as u32,
                    event_kind: if i % 2 == 0 { 1 } else { 2 },
                    call_depth: i % 10,
                    detail_seq: u32::MAX,
                };

                let event_bytes = unsafe {
                    std::slice::from_raw_parts(&event as *const IndexEvent as *const u8, 32)
                };
                index_file.write_all(event_bytes).unwrap();
            }

            // Write footer
            let footer = AtfIndexFooter {
                magic: *b"2ITA",
                checksum: 0,
                event_count: events_per_thread as u64,
                time_start_ns: 1000 + thread_id as u64 * 50,
                time_end_ns: 1000 + thread_id as u64 * 50 + events_per_thread as u64 * 100,
                bytes_written: events_per_thread as u64 * 32,
                reserved: [0; 24],
            };

            let footer_bytes = unsafe {
                std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
            };
            index_file.write_all(footer_bytes).unwrap();
            index_file.flush().unwrap();
        }

        dir
    }

    #[test]
    fn test_session_reader__opens_manifest__then_loads_threads() {
        // User Story: M1_E5_I2 - Load session with multiple threads
        // Test Plan: Integration Tests - Cross-Thread Merge-Sort
        let dir = create_test_session(4, 100);
        let session = SessionReader::open(dir.path()).unwrap();

        assert_eq!(session.threads().len(), 4);
        assert_eq!(session.event_count(), 400);
    }

    #[test]
    fn test_session_reader__time_range__then_correct() {
        // User Story: M1_E5_I2 - Calculate global time range
        // Test Plan: Integration Tests - Cross-Thread Merge-Sort
        let dir = create_test_session(4, 100);
        let session = SessionReader::open(dir.path()).unwrap();

        let (start, end) = session.time_range();
        assert!(start > 0);
        assert!(end > start);
    }

    #[test]
    fn test_merge_sort__multiple_threads__then_globally_ordered() {
        // User Story: M1_E5_I2 - Merge events from multiple threads
        // Test Plan: Integration Tests - Cross-Thread Merge-Sort
        let dir = create_test_session(4, 1000);
        let session = SessionReader::open(dir.path()).unwrap();

        let mut prev_timestamp = 0u64;
        let mut total_events = 0;

        for (_thread_idx, event) in session.merged_iter() {
            let timestamp = event.timestamp_ns;
            assert!(
                timestamp >= prev_timestamp,
                "Events must be globally ordered by timestamp" // LCOV_EXCL_LINE - Test assertion message
            );
            prev_timestamp = timestamp;
            total_events += 1;
        }

        assert_eq!(total_events, 4000);
    }

    #[test]
    fn test_merge_sort__single_thread__then_sequential() {
        // User Story: M1_E5_I2 - Merge-sort works with single thread
        // Test Plan: Integration Tests - Cross-Thread Merge-Sort
        let dir = create_test_session(1, 1000);
        let session = SessionReader::open(dir.path()).unwrap();

        let events: Vec<_> = session.merged_iter().collect();
        assert_eq!(events.len(), 1000);

        // All from thread 0
        for (thread_idx, _) in &events {
            assert_eq!(*thread_idx, 0);
        }
    }

    #[test]
    fn test_merge_sort__empty_session__then_no_events() {
        // User Story: M1_E5_I2 - Handle empty session
        // Test Plan: Integration Tests - Cross-Thread Merge-Sort
        let dir = TempDir::new().unwrap();

        let manifest = Manifest {
            threads: vec![],
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let manifest_str = serde_json::to_string_pretty(&manifest).unwrap();
        fs::write(dir.path().join("manifest.json"), manifest_str).unwrap();

        let session = SessionReader::open(dir.path()).unwrap();
        assert_eq!(session.threads().len(), 0);
        assert_eq!(session.event_count(), 0);

        let events: Vec<_> = session.merged_iter().collect();
        assert_eq!(events.len(), 0);
    }

    #[test]
    fn test_session_reader__empty_threads__then_time_range_zero() {
        // User Story: M1_E5_I2 - Handle session with no threads
        // Test Plan: Unit Tests - Session Reader
        let dir = TempDir::new().unwrap();

        let manifest = Manifest {
            threads: vec![],
            time_start_ns: 0,
            time_end_ns: 0,
        };

        let manifest_str = serde_json::to_string_pretty(&manifest).unwrap();
        fs::write(dir.path().join("manifest.json"), manifest_str).unwrap();

        let session = SessionReader::open(dir.path()).unwrap();
        let (start, end) = session.time_range();
        assert_eq!(start, 0);
        assert_eq!(end, 0);
    }

    #[test]
    fn test_session_reader__manifest_getter__then_returns_manifest() {
        // User Story: M1_E5_I2 - Access manifest data
        // Test Plan: Unit Tests - Session Reader
        let dir = create_test_session(2, 100);
        let session = SessionReader::open(dir.path()).unwrap();

        let manifest = session.manifest();
        assert_eq!(manifest.threads.len(), 2);
        assert_eq!(manifest.threads[0].id, 0);
        assert_eq!(manifest.threads[1].id, 1);
    }

    #[test]
    fn test_session_reader__thread_dir_missing__then_skips_thread() {
        // User Story: M1_E5_I2 - Handle missing thread directories
        // Test Plan: Error Handling Tests
        let dir = TempDir::new().unwrap();

        // Create manifest with 3 threads
        let manifest = Manifest {
            threads: vec![
                ThreadInfo { id: 0, has_detail: false },
                ThreadInfo { id: 1, has_detail: false },
                ThreadInfo { id: 2, has_detail: false },
            ],
            time_start_ns: 1000,
            time_end_ns: 2000,
        };

        let manifest_str = serde_json::to_string_pretty(&manifest).unwrap();
        fs::write(dir.path().join("manifest.json"), manifest_str).unwrap();

        // Only create thread_0 directory (skip thread_1 and thread_2)
        let thread_dir = dir.path().join("thread_0");
        fs::create_dir(&thread_dir).unwrap();

        use super::super::types::{AtfIndexFooter, AtfIndexHeader, IndexEvent};

        let index_path = thread_dir.join("index.atf");
        let mut index_file = fs::File::create(&index_path).unwrap();

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
            event_count: 1,
            events_offset: 64,
            footer_offset: 96,
            time_start_ns: 1000,
            time_end_ns: 1100,
        };

        let header_bytes = unsafe {
            std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
        };
        index_file.write_all(header_bytes).unwrap();

        let event = IndexEvent {
            timestamp_ns: 1000,
            function_id: 0x100000001,
            thread_id: 0,
            event_kind: 1,
            call_depth: 0,
            detail_seq: u32::MAX,
        };

        let event_bytes = unsafe {
            std::slice::from_raw_parts(&event as *const IndexEvent as *const u8, 32)
        };
        index_file.write_all(event_bytes).unwrap();

        let footer = AtfIndexFooter {
            magic: *b"2ITA",
            checksum: 0,
            event_count: 1,
            time_start_ns: 1000,
            time_end_ns: 1100,
            bytes_written: 32,
            reserved: [0; 24],
        };

        let footer_bytes = unsafe {
            std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
        };
        index_file.write_all(footer_bytes).unwrap();
        index_file.flush().unwrap();

        // Open session - should only load thread_0
        let session = SessionReader::open(dir.path()).unwrap();
        assert_eq!(session.threads().len(), 1);
        assert_eq!(session.threads()[0].thread_id(), 0);
    }
}
