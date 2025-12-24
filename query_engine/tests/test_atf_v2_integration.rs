// User Story: M1_E5_I2 - ATF V2 Reader Integration Tests
// Test Plan: M1_E5_I2_TEST_PLAN.md - Integration test categories
//
// These tests verify the ATF V2 reader implementation through comprehensive integration scenarios:
// 1. Round-trip tests (simulated C writer → Rust reader → verify)
// 2. Bidirectional navigation tests using real paired events
// 3. Cross-thread merge-sort tests with 4+ threads
// 4. Index-only session tests
// 5. Corrupted file handling tests
// 6. Large file tests
// 7. Crash recovery tests

use std::fs;
use std::io::Write;
use tempfile::TempDir;

// Import ATF V2 types and readers
use query_engine::atf::v2::{
    AtfDetailFooter, AtfDetailHeader, AtfIndexFooter, AtfIndexHeader, DetailEventHeader,
    IndexEvent, SessionReader, ThreadReader, ATF_DETAIL_EVENT_FUNCTION_CALL,
    ATF_DETAIL_EVENT_FUNCTION_RETURN, ATF_EVENT_KIND_CALL, ATF_EVENT_KIND_RETURN,
    ATF_INDEX_FLAG_HAS_DETAIL_FILE, ATF_NO_DETAIL_SEQ,
};

// ===== Helper Functions for Creating Test Fixtures =====

/// Create a valid index.atf file with specified events
fn create_test_index_file(
    path: &std::path::Path,
    thread_id: u32,
    events: &[(u64, u64, u32, u32, u32)], // (timestamp, function_id, event_kind, call_depth, detail_seq)
) -> std::io::Result<()> {
    let mut file = fs::File::create(path)?;

    let event_count = events.len() as u32;
    let time_start = events.first().map(|e| e.0).unwrap_or(0);
    let time_end = events.last().map(|e| e.0).unwrap_or(0);

    // Write header
    let header = AtfIndexHeader {
        magic: *b"ATI2",
        endian: 0x01,
        version: 1,
        arch: 1, // x86_64
        os: 3,   // macOS
        flags: if events.iter().any(|e| e.4 != ATF_NO_DETAIL_SEQ) {
            ATF_INDEX_FLAG_HAS_DETAIL_FILE
        } else {
            0
        },
        thread_id,
        clock_type: 1, // mach_continuous
        _reserved1: [0; 3],
        _reserved2: 0,
        event_size: 32,
        event_count,
        events_offset: 64,
        footer_offset: 64 + event_count as u64 * 32,
        time_start_ns: time_start,
        time_end_ns: time_end,
    };

    let header_bytes = unsafe {
        std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
    };
    file.write_all(header_bytes)?;

    // Write events
    for (timestamp, function_id, event_kind, call_depth, detail_seq) in events {
        let event = IndexEvent {
            timestamp_ns: *timestamp,
            function_id: *function_id,
            thread_id,
            event_kind: *event_kind,
            call_depth: *call_depth,
            detail_seq: *detail_seq,
        };

        let event_bytes =
            unsafe { std::slice::from_raw_parts(&event as *const IndexEvent as *const u8, 32) };
        file.write_all(event_bytes)?;
    }

    // Write footer
    let footer = AtfIndexFooter {
        magic: *b"2ITA",
        checksum: 0,
        event_count: event_count as u64,
        time_start_ns: time_start,
        time_end_ns: time_end,
        bytes_written: event_count as u64 * 32,
        reserved: [0; 24],
    };

    let footer_bytes = unsafe {
        std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
    };
    file.write_all(footer_bytes)?;

    file.flush()?;
    Ok(())
}

/// Create a valid detail.atf file with specified events
fn create_test_detail_file(
    path: &std::path::Path,
    thread_id: u32,
    events: &[(u16, u32, u64, Vec<u8>)], // (event_type, index_seq, timestamp, payload)
) -> std::io::Result<()> {
    let mut file = fs::File::create(path)?;

    let event_count = events.len() as u64;
    let time_start = events.first().map(|e| e.2).unwrap_or(0);
    let time_end = events.last().map(|e| e.2).unwrap_or(0);
    let bytes_length: usize = events.iter().map(|e| 24 + e.3.len()).sum();

    // Write header
    let header = AtfDetailHeader {
        magic: *b"ATD2",
        endian: 0x01,
        version: 1,
        arch: 1,
        os: 3,
        flags: 0,
        thread_id,
        _reserved1: 0,
        events_offset: 64,
        event_count,
        bytes_length: bytes_length as u64,
        index_seq_start: events.first().map(|e| e.1 as u64).unwrap_or(0),
        index_seq_end: events.last().map(|e| e.1 as u64).unwrap_or(0),
        _reserved2: [0; 4],
    };

    let header_bytes = unsafe {
        std::slice::from_raw_parts(&header as *const AtfDetailHeader as *const u8, 64)
    };
    file.write_all(header_bytes)?;

    // Write events
    for (event_type, index_seq, timestamp, payload) in events {
        let total_length = (24 + payload.len()) as u32;

        // Write DetailEventHeader
        file.write_all(&total_length.to_le_bytes())?;
        file.write_all(&event_type.to_le_bytes())?;
        file.write_all(&0u16.to_le_bytes())?; // flags
        file.write_all(&index_seq.to_le_bytes())?;
        file.write_all(&thread_id.to_le_bytes())?;
        file.write_all(&timestamp.to_le_bytes())?;

        // Write payload
        file.write_all(payload)?;
    }

    // Write footer
    let footer = AtfDetailFooter {
        magic: *b"2DTA",
        checksum: 0,
        event_count,
        bytes_length: bytes_length as u64,
        time_start_ns: time_start,
        time_end_ns: time_end,
        reserved: [0; 24],
    };

    let footer_bytes = unsafe {
        std::slice::from_raw_parts(&footer as *const AtfDetailFooter as *const u8, 64)
    };
    file.write_all(footer_bytes)?;

    file.flush()?;
    Ok(())
}

/// Create a full session directory with manifest and thread directories
fn create_test_session(dir: &TempDir, thread_configs: Vec<(u32, bool, u32)>) -> std::io::Result<()> {
    // thread_configs: (thread_id, has_detail, event_count)

    // Create manifest
    let manifest_path = dir.path().join("manifest.json");
    let threads: Vec<_> = thread_configs
        .iter()
        .map(|(id, has_detail, _)| {
            serde_json::json!({
                "id": id,
                "has_detail": has_detail
            })
        })
        .collect();

    let manifest = serde_json::json!({
        "threads": threads,
        "time_start_ns": 1000,
        "time_end_ns": 100000
    });

    fs::write(&manifest_path, serde_json::to_string_pretty(&manifest)?)?;

    // Create thread directories
    for (thread_id, has_detail, event_count) in thread_configs {
        let thread_dir = dir.path().join(format!("thread_{}", thread_id));
        fs::create_dir(&thread_dir)?;

        // Create index events with interleaved timestamps
        let mut index_events = Vec::new();
        for i in 0..event_count {
            let timestamp = 1000 + thread_id as u64 * 50 + i as u64 * 100;
            let function_id = 0x100000001 + i as u64;
            let event_kind = if i % 2 == 0 {
                ATF_EVENT_KIND_CALL
            } else {
                ATF_EVENT_KIND_RETURN
            };
            let call_depth = i % 10;
            let detail_seq = if has_detail { i } else { ATF_NO_DETAIL_SEQ };

            index_events.push((timestamp, function_id, event_kind, call_depth, detail_seq));
        }

        create_test_index_file(&thread_dir.join("index.atf"), thread_id, &index_events)?;

        // Create detail file if needed
        if has_detail {
            let mut detail_events = Vec::new();
            for i in 0..event_count {
                let timestamp = 1000 + thread_id as u64 * 50 + i as u64 * 100;
                let event_type = if i % 2 == 0 {
                    ATF_DETAIL_EVENT_FUNCTION_CALL
                } else {
                    ATF_DETAIL_EVENT_FUNCTION_RETURN
                };
                let payload = vec![i as u8; 80]; // 80 bytes of payload

                detail_events.push((event_type, i, timestamp, payload));
            }

            create_test_detail_file(&thread_dir.join("detail.atf"), thread_id, &detail_events)?;
        }
    }

    Ok(())
}

// ===== Integration Test Cases =====

#[test]
fn round_trip__simulated_c_writer_to_rust__then_events_match() {
    // User Story: M1_E5_I2 - Verify Rust reader can read files created by simulated C writer
    // Test Plan: Integration Tests - Round-trip tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, true, 100)]).unwrap();

    let session = SessionReader::open(dir.path()).unwrap();
    assert_eq!(session.threads().len(), 1);

    let thread = &session.threads()[0];
    assert_eq!(thread.index.len(), 100);
    assert!(thread.detail.is_some());
    assert_eq!(thread.detail.as_ref().unwrap().len(), 100);

    // Verify first event (copy values to avoid packed struct alignment issues)
    let first_index = thread.index.get(0).unwrap();
    let timestamp = first_index.timestamp_ns;
    let function_id = first_index.function_id;
    let event_kind = first_index.event_kind;
    assert_eq!(timestamp, 1000);
    assert_eq!(function_id, 0x100000001);
    assert_eq!(event_kind, ATF_EVENT_KIND_CALL);
}

#[test]
fn bidirectional__forward_lookup__then_detail_found() {
    // User Story: M1_E5_I2 - Forward navigation from index to detail
    // Test Plan: Integration Tests - Bidirectional navigation tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, true, 100)]).unwrap();

    let thread_dir = dir.path().join("thread_0");
    let thread = ThreadReader::open(&thread_dir).unwrap();

    // Test forward lookup for event 50
    let index_event = thread.index.get(50).unwrap();
    let detail_seq = index_event.detail_seq;
    let index_timestamp = index_event.timestamp_ns;
    assert_eq!(detail_seq, 50);

    let detail = thread.get_detail_for(index_event).unwrap();
    let header = detail.header();
    let detail_index_seq = header.index_seq;
    let detail_timestamp = header.timestamp;
    assert_eq!(detail_index_seq, 50);
    assert_eq!(detail_timestamp, index_timestamp);
}

#[test]
fn bidirectional__backward_lookup__then_index_found() {
    // User Story: M1_E5_I2 - Backward navigation from detail to index
    // Test Plan: Integration Tests - Bidirectional navigation tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, true, 100)]).unwrap();

    let thread_dir = dir.path().join("thread_0");
    let thread = ThreadReader::open(&thread_dir).unwrap();

    // Test backward lookup for detail event 25
    let detail = thread.detail.as_ref().unwrap().get(25).unwrap();
    let header = detail.header();
    let detail_index_seq = header.index_seq;
    assert_eq!(detail_index_seq, 25);

    let index = thread.get_index_for(&detail).unwrap();
    let index_detail_seq = index.detail_seq;
    assert_eq!(index_detail_seq, 25);
}

#[test]
fn bidirectional__round_trip_all_events__then_consistent() {
    // User Story: M1_E5_I2 - Verify bidirectional consistency for all events
    // Test Plan: Integration Tests - Bidirectional navigation tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, true, 100)]).unwrap();

    let thread_dir = dir.path().join("thread_0");
    let thread = ThreadReader::open(&thread_dir).unwrap();

    // For each index event, verify round-trip consistency
    for seq in 0..100 {
        let index_event = thread.index.get(seq).unwrap();
        let index_timestamp = index_event.timestamp_ns;
        let index_detail_seq = index_event.detail_seq;

        // Forward: index → detail
        let detail = thread.get_detail_for(index_event).unwrap();
        let detail_header = detail.header();
        let detail_timestamp = detail_header.timestamp;
        let detail_index_seq_val = detail_header.index_seq;

        // Backward: detail → index
        let back_to_index = thread.get_index_for(&detail).unwrap();
        let back_timestamp = back_to_index.timestamp_ns;

        // Verify consistency
        assert_eq!(index_timestamp, back_timestamp);
        assert_eq!(index_timestamp, detail_timestamp);
        assert_eq!(index_detail_seq, seq);
        assert_eq!(detail_index_seq_val, seq);
    }
}

#[test]
fn merge_sort__four_threads_interleaved__then_globally_ordered() {
    // User Story: M1_E5_I2 - Merge events from 4 threads in global timestamp order
    // Test Plan: Integration Tests - Cross-thread merge-sort tests
    let dir = TempDir::new().unwrap();
    create_test_session(
        &dir,
        vec![
            (0, false, 250),
            (1, false, 250),
            (2, false, 250),
            (3, false, 250),
        ],
    )
    .unwrap();

    let session = SessionReader::open(dir.path()).unwrap();
    assert_eq!(session.threads().len(), 4);
    assert_eq!(session.event_count(), 1000);

    // Verify merge-sort produces globally ordered events
    let mut prev_timestamp = 0u64;
    let mut event_count = 0;

    for (_thread_idx, event) in session.merged_iter() {
        let timestamp = event.timestamp_ns;
        assert!(
            timestamp >= prev_timestamp,
            "Events must be globally ordered by timestamp: {} >= {}",
            timestamp,
            prev_timestamp
        );
        prev_timestamp = timestamp;
        event_count += 1;
    }

    assert_eq!(event_count, 1000);
}

#[test]
fn merge_sort__empty_threads__then_handles_gracefully() {
    // User Story: M1_E5_I2 - Handle empty threads in merge-sort
    // Test Plan: Integration Tests - Cross-thread merge-sort tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![]).unwrap();

    let session = SessionReader::open(dir.path()).unwrap();
    assert_eq!(session.threads().len(), 0);
    assert_eq!(session.event_count(), 0);

    let events: Vec<_> = session.merged_iter().collect();
    assert_eq!(events.len(), 0);
}

#[test]
fn index_only__missing_detail_file__then_reads_index() {
    // User Story: M1_E5_I2 - Read index file without detail file
    // Test Plan: Integration Tests - Index-only session tests
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, false, 100)]).unwrap();

    let thread_dir = dir.path().join("thread_0");
    let thread = ThreadReader::open(&thread_dir).unwrap();

    assert_eq!(thread.index.len(), 100);
    assert!(thread.detail.is_none());

    // Verify all events have no detail_seq
    for seq in 0..100 {
        let event = thread.index.get(seq).unwrap();
        let detail_seq = event.detail_seq;
        assert_eq!(detail_seq, ATF_NO_DETAIL_SEQ);
    }
}

#[test]
fn corrupted__invalid_magic__then_error_returned() {
    // User Story: M1_E5_I2 - Reject files with invalid magic bytes
    // Test Plan: Integration Tests - Corrupted file handling tests
    let dir = TempDir::new().unwrap();
    let index_path = dir.path().join("index.atf");

    // Create file with invalid magic
    let mut file = fs::File::create(&index_path).unwrap();
    let mut header_bytes = vec![0u8; 64];
    header_bytes[0..4].copy_from_slice(b"XXXX"); // Invalid magic
    header_bytes[4] = 0x01; // endian
    header_bytes[5] = 1; // version
    file.write_all(&header_bytes).unwrap();
    file.flush().unwrap();
    drop(file);

    // Try to open - should fail
    let result = query_engine::atf::v2::IndexReader::open(&index_path);
    assert!(result.is_err());
}

#[test]
fn corrupted__truncated_header__then_error_returned() {
    // User Story: M1_E5_I2 - Reject truncated files
    // Test Plan: Integration Tests - Corrupted file handling tests
    let dir = TempDir::new().unwrap();
    let index_path = dir.path().join("index.atf");

    // Create file with truncated header (only 32 bytes)
    let mut file = fs::File::create(&index_path).unwrap();
    file.write_all(&[0u8; 32]).unwrap();
    file.flush().unwrap();
    drop(file);

    // Try to open - should fail
    let result = query_engine::atf::v2::IndexReader::open(&index_path);
    assert!(result.is_err());
}

#[test]
fn corrupted__mismatched_counts__then_uses_footer() {
    // User Story: M1_E5_I2 - Use footer count when header count mismatches
    // Test Plan: Integration Tests - Corrupted file handling tests
    let dir = TempDir::new().unwrap();
    let index_path = dir.path().join("index.atf");

    // Create file with mismatched header count (claims 100 events) but footer says 50
    let mut file = fs::File::create(&index_path).unwrap();

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
        event_count: 100, // Claims 100
        events_offset: 64,
        footer_offset: 64 + 50 * 32, // But footer is at 50 events
        time_start_ns: 1000,
        time_end_ns: 6000,
    };

    let header_bytes = unsafe {
        std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
    };
    file.write_all(header_bytes).unwrap();

    // Write 50 events
    for i in 0..50 {
        let event = IndexEvent {
            timestamp_ns: 1000 + i as u64 * 100,
            function_id: 0x100000001 + i as u64,
            thread_id: 0,
            event_kind: ATF_EVENT_KIND_CALL,
            call_depth: 0,
            detail_seq: ATF_NO_DETAIL_SEQ,
        };

        let event_bytes =
            unsafe { std::slice::from_raw_parts(&event as *const IndexEvent as *const u8, 32) };
        file.write_all(event_bytes).unwrap();
    }

    // Write valid footer with correct count
    let footer = AtfIndexFooter {
        magic: *b"2ITA",
        checksum: 0,
        event_count: 50, // Correct count
        time_start_ns: 1000,
        time_end_ns: 6000,
        bytes_written: 50 * 32,
        reserved: [0; 24],
    };

    let footer_bytes = unsafe {
        std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
    };
    file.write_all(footer_bytes).unwrap();
    file.flush().unwrap();
    drop(file);

    // Reader should use footer count (50) instead of header count (100)
    let reader = query_engine::atf::v2::IndexReader::open(&index_path).unwrap();
    assert_eq!(reader.len(), 50, "Reader should use footer count");
}

#[test]
fn large_file__100k_events__then_no_memory_explosion() {
    // User Story: M1_E5_I2 - Handle large files without memory explosion
    // Test Plan: Integration Tests - Large file test (using 100K for faster tests)
    let dir = TempDir::new().unwrap();
    create_test_session(&dir, vec![(0, false, 100_000)]).unwrap();

    let session = SessionReader::open(dir.path()).unwrap();
    assert_eq!(session.event_count(), 100_000);

    // Memory-mapped access should not load entire file into memory
    // Just verify we can access first and last events
    let thread = &session.threads()[0];
    let first = thread.index.get(0).unwrap();
    let last = thread.index.get(99_999).unwrap();
    let first_ts = first.timestamp_ns;
    let last_ts = last.timestamp_ns;

    assert!(first_ts < last_ts);
}

#[test]
fn crash_recovery__truncated_events_valid_footer__then_uses_footer_count() {
    // User Story: M1_E5_I2 - Crash recovery using footer
    // Test Plan: Integration Tests - Crash recovery test
    let dir = TempDir::new().unwrap();
    let index_path = dir.path().join("index.atf");

    // Simulate crash scenario: header claims 100 events, but only 60 were written
    // Footer has authoritative count of 60
    let mut file = fs::File::create(&index_path).unwrap();

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
        event_count: 100, // Claimed count before crash
        events_offset: 64,
        footer_offset: 64 + 60 * 32, // Footer written after 60 events
        time_start_ns: 1000,
        time_end_ns: 7000,
    };

    let header_bytes = unsafe {
        std::slice::from_raw_parts(&header as *const AtfIndexHeader as *const u8, 64)
    };
    file.write_all(header_bytes).unwrap();

    // Write 60 events (crash happened before writing all 100)
    for i in 0..60 {
        let event = IndexEvent {
            timestamp_ns: 1000 + i as u64 * 100,
            function_id: 0x100000001 + i as u64,
            thread_id: 0,
            event_kind: ATF_EVENT_KIND_CALL,
            call_depth: 0,
            detail_seq: ATF_NO_DETAIL_SEQ,
        };

        let event_bytes =
            unsafe { std::slice::from_raw_parts(&event as *const IndexEvent as *const u8, 32) };
        file.write_all(event_bytes).unwrap();
    }

    // Write footer with actual count
    let footer = AtfIndexFooter {
        magic: *b"2ITA",
        checksum: 0,
        event_count: 60, // Actual events written
        time_start_ns: 1000,
        time_end_ns: 7000,
        bytes_written: 60 * 32,
        reserved: [0; 24],
    };

    let footer_bytes = unsafe {
        std::slice::from_raw_parts(&footer as *const AtfIndexFooter as *const u8, 64)
    };
    file.write_all(footer_bytes).unwrap();
    file.flush().unwrap();
    drop(file);

    // Reader should use footer's authoritative count
    let reader = query_engine::atf::v2::IndexReader::open(&index_path).unwrap();
    assert_eq!(
        reader.len(),
        60,
        "Reader should use footer count (authoritative): got {}, expected 60",
        reader.len()
    );

    // Verify all 60 events are readable
    for seq in 0..60 {
        let event = reader.get(seq).unwrap();
        let timestamp = event.timestamp_ns;
        assert_eq!(timestamp, 1000 + seq as u64 * 100);
    }
}

#[test]
fn merge_sort__threads_with_gaps__then_correctly_ordered() {
    // User Story: M1_E5_I2 - Handle threads with non-overlapping time ranges
    // Test Plan: Integration Tests - Cross-thread merge-sort tests
    let dir = TempDir::new().unwrap();

    // Create session with 3 threads having distinct time ranges
    let thread_dir_0 = dir.path().join("thread_0");
    fs::create_dir(&thread_dir_0).unwrap();
    create_test_index_file(
        &thread_dir_0.join("index.atf"),
        0,
        &[
            (1000, 0x100000001, ATF_EVENT_KIND_CALL, 0, ATF_NO_DETAIL_SEQ),
            (2000, 0x100000002, ATF_EVENT_KIND_RETURN, 0, ATF_NO_DETAIL_SEQ),
        ],
    )
    .unwrap();

    let thread_dir_1 = dir.path().join("thread_1");
    fs::create_dir(&thread_dir_1).unwrap();
    create_test_index_file(
        &thread_dir_1.join("index.atf"),
        1,
        &[
            (5000, 0x200000001, ATF_EVENT_KIND_CALL, 0, ATF_NO_DETAIL_SEQ),
            (6000, 0x200000002, ATF_EVENT_KIND_RETURN, 0, ATF_NO_DETAIL_SEQ),
        ],
    )
    .unwrap();

    let thread_dir_2 = dir.path().join("thread_2");
    fs::create_dir(&thread_dir_2).unwrap();
    create_test_index_file(
        &thread_dir_2.join("index.atf"),
        2,
        &[
            (3000, 0x300000001, ATF_EVENT_KIND_CALL, 0, ATF_NO_DETAIL_SEQ),
            (4000, 0x300000002, ATF_EVENT_KIND_RETURN, 0, ATF_NO_DETAIL_SEQ),
        ],
    )
    .unwrap();

    // Create manifest
    let manifest = serde_json::json!({
        "threads": [
            {"id": 0, "has_detail": false},
            {"id": 1, "has_detail": false},
            {"id": 2, "has_detail": false}
        ],
        "time_start_ns": 1000,
        "time_end_ns": 6000
    });
    fs::write(
        dir.path().join("manifest.json"),
        serde_json::to_string_pretty(&manifest).unwrap(),
    )
    .unwrap();

    let session = SessionReader::open(dir.path()).unwrap();

    // Collect all timestamps in merge order
    let timestamps: Vec<_> = session
        .merged_iter()
        .map(|(_, event)| event.timestamp_ns)
        .collect();

    // Verify correct global order: 1000, 2000, 3000, 4000, 5000, 6000
    assert_eq!(timestamps, vec![1000, 2000, 3000, 4000, 5000, 6000]);
}

#[test]
fn session__time_range__then_spans_all_threads() {
    // User Story: M1_E5_I2 - Calculate global time range across all threads
    // Test Plan: Integration Tests - Cross-thread merge-sort tests
    let dir = TempDir::new().unwrap();
    create_test_session(
        &dir,
        vec![(0, false, 10), (1, false, 10), (2, false, 10)],
    )
    .unwrap();

    let session = SessionReader::open(dir.path()).unwrap();
    let (start, end) = session.time_range();

    // Thread 0 starts at 1000, Thread 2 ends later
    assert_eq!(start, 1000);
    assert!(end > start);
}

#[test]
fn detail_reader__variable_length_payloads__then_correctly_parsed() {
    // User Story: M1_E5_I2 - Parse variable-length detail events
    // Test Plan: Integration Tests - Round-trip tests
    let dir = TempDir::new().unwrap();
    let detail_path = dir.path().join("detail.atf");

    // Create detail file with variable-length payloads
    let events = vec![
        (
            ATF_DETAIL_EVENT_FUNCTION_CALL,
            0,
            1000,
            vec![1u8; 50], // 50 bytes
        ),
        (
            ATF_DETAIL_EVENT_FUNCTION_RETURN,
            1,
            2000,
            vec![2u8; 100], // 100 bytes
        ),
        (
            ATF_DETAIL_EVENT_FUNCTION_CALL,
            2,
            3000,
            vec![3u8; 25], // 25 bytes
        ),
    ];

    create_test_detail_file(&detail_path, 0, &events).unwrap();

    let reader = query_engine::atf::v2::DetailReader::open(&detail_path).unwrap();
    assert_eq!(reader.len(), 3);

    // Verify each event's payload length
    assert_eq!(reader.get(0).unwrap().payload().len(), 50);
    assert_eq!(reader.get(1).unwrap().payload().len(), 100);
    assert_eq!(reader.get(2).unwrap().payload().len(), 25);
}
