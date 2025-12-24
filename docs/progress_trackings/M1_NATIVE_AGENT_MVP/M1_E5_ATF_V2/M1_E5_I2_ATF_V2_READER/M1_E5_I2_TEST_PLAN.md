---
status: completed
date: 2025-12-23
supersedes: M1_E4_I1_ATF_READER
---

# M1_E5_I2 Test Plan: ATF V2 Reader

## Test Coverage Map

| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Index Header Parsing | ✓ | - | - |
| Index Event Access | ✓ | - | - |
| Index Footer Recovery | ✓ | - | - |
| Detail Header Parsing | ✓ | - | - |
| Detail Event Access | ✓ | - | - |
| Detail Index Building | ✓ | - | ✓ |
| Forward Lookup | ✓ | ✓ | - |
| Backward Lookup | ✓ | ✓ | - |
| Cross-Thread Merge | - | ✓ | ✓ |
| Round-trip (Writer→Reader) | - | ✓ | - |
| Python Bindings | - | ✓ | - |
| Error Handling | ✓ | ✓ | - |
| Read Throughput | - | - | ✓ |

## Unit Tests

### 1. Index Header Parsing

```rust
#[test]
fn test_index_header__valid_magic__then_parsed() {
    let header = create_valid_index_header();
    let result = AtfIndexHeader::validate(&header);
    assert!(result.is_ok());
    assert_eq!(&header.magic, b"ATI2");
}

#[test]
fn test_index_header__invalid_magic__then_error() {
    let mut header = create_valid_index_header();
    header.magic = *b"XXXX";
    let result = AtfIndexHeader::validate(&header);
    assert!(matches!(result, Err(AtfError::InvalidMagic)));
}

#[test]
fn test_index_header__wrong_version__then_error() {
    let mut header = create_valid_index_header();
    header.version = 99;
    let result = AtfIndexHeader::validate(&header);
    assert!(matches!(result, Err(AtfError::UnsupportedVersion(99))));
}

#[test]
fn test_index_header__big_endian__then_error() {
    let mut header = create_valid_index_header();
    header.endian = 0x00;  // Big endian
    let result = AtfIndexHeader::validate(&header);
    assert!(matches!(result, Err(AtfError::UnsupportedEndian)));
}
```

### 2. Index Event Access

```rust
#[test]
fn test_index_reader__get_first_event__then_correct() {
    let reader = create_test_index_reader(100);
    let event = reader.get(0).unwrap();
    assert_eq!(event.timestamp_ns, 1000);
    assert_eq!(event.function_id, 0x100000001);
}

#[test]
fn test_index_reader__get_last_event__then_correct() {
    let reader = create_test_index_reader(100);
    let event = reader.get(99).unwrap();
    assert!(event.timestamp_ns >= 1000);
}

#[test]
fn test_index_reader__out_of_bounds__then_none() {
    let reader = create_test_index_reader(100);
    let event = reader.get(100);
    assert!(event.is_none());
}

#[test]
fn test_index_reader__iteration__then_sequential() {
    let reader = create_test_index_reader(100);
    let events: Vec<_> = reader.iter().collect();
    assert_eq!(events.len(), 100);

    // Verify timestamps are non-decreasing
    for window in events.windows(2) {
        assert!(window[0].timestamp_ns <= window[1].timestamp_ns);
    }
}
```

### 3. Index Footer Recovery

```rust
#[test]
fn test_index_reader__valid_footer__then_uses_footer_count() {
    let reader = create_test_reader_with_footer(100, 100);
    assert_eq!(reader.len(), 100);
}

#[test]
fn test_index_reader__corrupted_header_count__then_uses_footer() {
    // Create file with header.event_count=999 but footer.event_count=100
    let reader = create_test_reader_with_mismatch(999, 100);
    assert_eq!(reader.len(), 100);  // Footer is authoritative
}

#[test]
fn test_index_reader__missing_footer__then_calculates_from_size() {
    let reader = create_test_reader_without_footer(100);
    assert_eq!(reader.len(), 100);  // Calculated from file size
}
```

### 4. Detail Header Parsing

```rust
#[test]
fn test_detail_header__valid_magic__then_parsed() {
    let header = create_valid_detail_header();
    let result = AtfDetailHeader::validate(&header);
    assert!(result.is_ok());
    assert_eq!(&header.magic, b"ATD2");
}

#[test]
fn test_detail_header__invalid_magic__then_error() {
    let mut header = create_valid_detail_header();
    header.magic = *b"XXXX";
    let result = AtfDetailHeader::validate(&header);
    assert!(matches!(result, Err(AtfError::InvalidMagic)));
}
```

### 5. Detail Event Access

```rust
#[test]
fn test_detail_reader__get_by_seq__then_correct() {
    let reader = create_test_detail_reader(50);
    let event = reader.get(0).unwrap();
    assert_eq!(event.header().event_type, 3);  // FUNCTION_CALL
}

#[test]
fn test_detail_reader__variable_length__then_parsed() {
    let reader = create_test_detail_reader_variable(10);
    for seq in 0..10 {
        let event = reader.get(seq).unwrap();
        assert!(event.payload().len() > 0);
    }
}

#[test]
fn test_detail_reader__out_of_bounds__then_none() {
    let reader = create_test_detail_reader(50);
    let event = reader.get(50);
    assert!(event.is_none());
}
```

### 6. Bidirectional Navigation

```rust
#[test]
fn test_forward_lookup__has_detail__then_returns_event() {
    let thread = create_test_thread_reader_with_detail(100);
    let index_event = thread.index.get(10).unwrap();

    assert_ne!(index_event.detail_seq, u32::MAX);

    let detail = thread.get_detail_for(index_event);
    assert!(detail.is_some());
    assert_eq!(detail.unwrap().header().index_seq, 10);
}

#[test]
fn test_forward_lookup__no_detail__then_none() {
    let thread = create_test_thread_reader_with_detail(100);

    // Find an event without detail (detail_seq = MAX)
    let index_event = create_index_event_without_detail();
    let detail = thread.get_detail_for(&index_event);
    assert!(detail.is_none());
}

#[test]
fn test_backward_lookup__valid_index_seq__then_returns_event() {
    let thread = create_test_thread_reader_with_detail(100);
    let detail_event = thread.detail.as_ref().unwrap().get(5).unwrap();

    let index = thread.get_index_for(&detail_event);
    assert!(index.is_some());
    assert_eq!(index.unwrap().detail_seq, 5);
}

#[test]
fn test_bidirectional__round_trip__then_consistent() {
    let thread = create_test_thread_reader_with_detail(100);

    // For each index event with detail, verify round-trip
    for index_event in thread.index.iter() {
        if index_event.detail_seq == u32::MAX {
            continue;
        }

        // Forward: index → detail
        let detail = thread.get_detail_for(index_event).unwrap();

        // Backward: detail → index
        let back_to_index = thread.get_index_for(&detail).unwrap();

        // Should be same event
        assert_eq!(back_to_index.timestamp_ns, index_event.timestamp_ns);
    }
}
```

## Integration Tests

### 1. Round-Trip with Writer

```rust
#[test]
fn test_round_trip__writer_to_reader__events_match() {
    let temp_dir = tempdir().unwrap();

    // Write 1000 events
    let writer = AtfThreadWriter::create(temp_dir.path(), 0);
    for i in 0..1000 {
        let idx_seq = writer.write_index_event(
            i * 100,      // timestamp_ns
            0x100000001,  // function_id
            1,            // event_kind (CALL)
            i % 10,       // call_depth
            u32::MAX,     // detail_seq (no detail)
        );
    }
    writer.finalize();
    writer.close();

    // Read back
    let reader = IndexReader::open(&temp_dir.path().join("thread_0/index.atf")).unwrap();
    assert_eq!(reader.len(), 1000);

    for i in 0..1000 {
        let event = reader.get(i).unwrap();
        assert_eq!(event.timestamp_ns, i as u64 * 100);
    }
}
```

### 2. Bidirectional Links Valid

```rust
#[test]
fn test_integration__bidirectional_links__all_valid() {
    let temp_dir = tempdir().unwrap();

    // Write paired events
    let writer = AtfThreadWriter::create(temp_dir.path(), 0);
    for i in 0..100 {
        let (idx_seq, det_seq) = writer.reserve_sequences(true);
        writer.write_index_event(..., det_seq);
        writer.write_detail_event(idx_seq, ...);
    }
    writer.finalize();
    writer.close();

    // Read and verify links
    let thread = ThreadReader::open(&temp_dir.path().join("thread_0")).unwrap();

    for i in 0..100 {
        let index_event = thread.index.get(i).unwrap();

        // Forward link
        let detail = thread.get_detail_for(index_event).unwrap();
        assert_eq!(detail.header().index_seq, i);

        // Backward link
        let back = thread.get_index_for(&detail).unwrap();
        assert_eq!(back.detail_seq, index_event.detail_seq);
    }
}
```

### 3. Cross-Thread Merge-Sort

```rust
#[test]
fn test_merge_sort__multiple_threads__then_globally_ordered() {
    let session = create_test_session_multi_thread(4, 1000);  // 4 threads, 1000 events each

    let mut prev_timestamp = 0u64;
    let mut total_events = 0;

    for (thread_idx, event) in session.merged_iter() {
        assert!(event.timestamp_ns >= prev_timestamp,
            "Events must be globally ordered by timestamp");
        prev_timestamp = event.timestamp_ns;
        total_events += 1;
    }

    assert_eq!(total_events, 4000);
}
```

### 4. Index-Only Session

```rust
#[test]
fn test_session__index_only__no_detail_file() {
    let session = create_test_session_index_only(1000);

    let thread = &session.threads()[0];
    assert!(thread.detail.is_none());
    assert!(!thread.index.has_detail());

    for event in thread.index.iter() {
        assert_eq!(event.detail_seq, u32::MAX);
    }
}
```

### 5. Python Round-Trip

```python
def test_python__read_rust_written_atf():
    """Test Python can read ATF files written by Rust/C."""
    session_dir = Path("/tmp/test_session")

    # Read with Python
    reader = SessionReader(session_dir)

    assert reader.event_count() == 1000

    for event in reader.iter_events():
        assert event.timestamp_ns > 0
        assert event.function_id > 0
```

## Performance Tests

### 1. Sequential Read Throughput

```rust
#[test]
fn test_performance__sequential_read__exceeds_1GB_per_sec() {
    let session = create_large_test_session(10_000_000);  // 10M events
    let file_size = 10_000_000 * 32;  // 320 MB

    let start = Instant::now();
    let mut count = 0;
    for event in session.threads()[0].index.iter() {
        count += 1;
        black_box(event);  // Prevent optimization
    }
    let elapsed = start.elapsed();

    let throughput_gb = (file_size as f64 / 1e9) / elapsed.as_secs_f64();

    assert_eq!(count, 10_000_000);
    assert!(throughput_gb > 1.0, "Throughput {:.2} GB/s below target", throughput_gb);
}
```

### 2. Random Access Latency

```rust
#[test]
fn test_performance__random_access__under_1us() {
    let reader = create_test_index_reader(1_000_000);
    let mut rng = rand::thread_rng();

    let mut latencies = Vec::with_capacity(10000);

    for _ in 0..10000 {
        let seq = rng.gen_range(0..1_000_000);

        let start = Instant::now();
        let event = reader.get(seq);
        let latency = start.elapsed();

        black_box(event);
        latencies.push(latency);
    }

    let avg_latency = latencies.iter().sum::<Duration>() / latencies.len() as u32;
    assert!(avg_latency < Duration::from_micros(1),
        "Average latency {:?} above target", avg_latency);
}
```

### 3. Detail Index Building

```rust
#[test]
fn test_performance__detail_index_build__under_100ms() {
    let temp_file = create_large_detail_file(100_000);  // 100K detail events

    let start = Instant::now();
    let reader = DetailReader::open(&temp_file).unwrap();
    let build_time = start.elapsed();

    assert!(build_time < Duration::from_millis(100),
        "Index build time {:?} above target", build_time);
}
```

### 4. Cross-Thread Merge Performance

```rust
#[test]
fn test_performance__merge_sort__linear_scaling() {
    let session = create_test_session_multi_thread(8, 100_000);  // 8 threads, 100K each

    let start = Instant::now();
    let count = session.merged_iter().count();
    let elapsed = start.elapsed();

    assert_eq!(count, 800_000);

    let events_per_sec = count as f64 / elapsed.as_secs_f64();
    assert!(events_per_sec > 10_000_000.0,  // 10M events/sec merge
        "Merge throughput {:.2}M/sec below target", events_per_sec / 1e6);
}
```

## Error Handling Tests

```rust
#[test]
fn test_error__missing_index_file__then_io_error() {
    let result = IndexReader::open(Path::new("/nonexistent/index.atf"));
    assert!(matches!(result, Err(AtfError::Io(_))));
}

#[test]
fn test_error__truncated_header__then_error() {
    let file = create_truncated_file(32);  // Only 32 bytes, header needs 64
    let result = IndexReader::open(&file);
    assert!(result.is_err());
}

#[test]
fn test_error__corrupted_magic__then_invalid_magic() {
    let file = create_file_with_bad_magic();
    let result = IndexReader::open(&file);
    assert!(matches!(result, Err(AtfError::InvalidMagic)));
}

#[test]
fn test_error__truncated_events__then_partial_read() {
    // Header says 100 events, but file only contains 50
    let file = create_truncated_events_file(100, 50);
    let reader = IndexReader::open(&file).unwrap();

    // Should only return 50 events (based on actual file size)
    assert_eq!(reader.len(), 50);
}
```

## Test Fixtures

### Session Directory Setup

```rust
fn create_test_session_dir() -> TempDir {
    let dir = tempdir().unwrap();

    // Create manifest
    let manifest = json!({
        "threads": [
            {"id": 0, "has_detail": true},
            {"id": 1, "has_detail": false}
        ],
        "time_start_ns": 1000,
        "time_end_ns": 10000
    });
    std::fs::write(dir.path().join("manifest.json"), manifest.to_string()).unwrap();

    // Create thread directories
    std::fs::create_dir(dir.path().join("thread_0")).unwrap();
    std::fs::create_dir(dir.path().join("thread_1")).unwrap();

    dir
}
```

## Success Criteria

1. All unit tests pass
2. All integration tests pass
3. Round-trip with writer succeeds (Rust and Python)
4. Bidirectional navigation O(1) in both directions
5. Cross-thread merge-sort produces globally ordered events
6. Sequential read throughput exceeds 1 GB/sec
7. Random access latency under 1 microsecond
8. Error handling is graceful for all corruption scenarios
9. 100% coverage on new code
