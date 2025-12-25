---
id: M1_E7_I1-tests
iteration: M1_E7_I1
---

# M1_E7_I1 Test Plan: ATF V2 Writer

## Test Coverage Map

| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Index Header Layout | ✓ | - | - |
| Index Event Layout | ✓ | - | - |
| Index Footer Layout | ✓ | - | - |
| Detail Header Layout | ✓ | - | - |
| Detail Event Layout | ✓ | - | - |
| Detail Footer Layout | ✓ | - | - |
| Bidirectional Linking | ✓ | ✓ | - |
| Forward Lookup | ✓ | ✓ | - |
| Backward Lookup | ✓ | ✓ | - |
| Writer API | ✓ | ✓ | - |
| Drain Integration | - | ✓ | - |
| Round-trip (Rust) | - | ✓ | - |
| Round-trip (Python) | - | ✓ | - |
| Index-only Mode | - | ✓ | - |
| Index + Detail Mode | - | ✓ | - |
| Crash Recovery | - | ✓ | - |
| Write Throughput | - | - | ✓ |

## Unit Tests

### 1. Index Header Byte Layout

```c
TEST(atf_index_header__size__equals_64_bytes) {
    _Static_assert(sizeof(AtfIndexHeader) == 64, "Header must be 64 bytes");
}

TEST(atf_index_header__magic__is_ATI2) {
    AtfIndexHeader header = {0};
    atf_init_index_header(&header);
    EXPECT_EQ(memcmp(header.magic, "ATI2", 4), 0);
}

TEST(atf_index_header__endian__is_little) {
    AtfIndexHeader header = {0};
    atf_init_index_header(&header);
    EXPECT_EQ(header.endian, 0x01);
}
```

### 2. Index Event Byte Layout

```c
TEST(index_event__size__equals_32_bytes) {
    _Static_assert(sizeof(IndexEvent) == 32, "Event must be 32 bytes");
}

TEST(index_event__detail_seq_max__indicates_no_detail) {
    IndexEvent event = {0};
    event.detail_seq = UINT32_MAX;
    EXPECT_FALSE(index_event_has_detail(&event));
}

TEST(index_event__detail_seq_valid__indicates_has_detail) {
    IndexEvent event = {0};
    event.detail_seq = 42;
    EXPECT_TRUE(index_event_has_detail(&event));
}
```

### 3. Index Footer Byte Layout

```c
TEST(atf_index_footer__size__equals_64_bytes) {
    _Static_assert(sizeof(AtfIndexFooter) == 64, "Footer must be 64 bytes");
}

TEST(atf_index_footer__magic__is_reversed) {
    AtfIndexFooter footer = {0};
    atf_init_index_footer(&footer);
    EXPECT_EQ(memcmp(footer.magic, "2ITA", 4), 0);
}
```

### 4. Detail Header Byte Layout

```c
TEST(atf_detail_header__size__equals_64_bytes) {
    _Static_assert(sizeof(AtfDetailHeader) == 64, "Header must be 64 bytes");
}

TEST(atf_detail_header__magic__is_ATD2) {
    AtfDetailHeader header = {0};
    atf_init_detail_header(&header);
    EXPECT_EQ(memcmp(header.magic, "ATD2", 4), 0);
}
```

### 5. Bidirectional Sequence Generation

```c
TEST(thread_counters__reserve_both__links_correctly) {
    ThreadCounters tc = {0};
    bool detail_enabled = true;

    uint32_t idx_seq, det_seq;
    atf_reserve_sequences(&tc, detail_enabled, &idx_seq, &det_seq);

    EXPECT_EQ(idx_seq, 0);
    EXPECT_EQ(det_seq, 0);
    EXPECT_EQ(tc.index_count, 1);
    EXPECT_EQ(tc.detail_count, 1);
}

TEST(thread_counters__no_detail__sets_max) {
    ThreadCounters tc = {0};
    bool detail_enabled = false;

    uint32_t idx_seq, det_seq;
    atf_reserve_sequences(&tc, detail_enabled, &idx_seq, &det_seq);

    EXPECT_EQ(idx_seq, 0);
    EXPECT_EQ(det_seq, UINT32_MAX);
    EXPECT_EQ(tc.index_count, 1);
    EXPECT_EQ(tc.detail_count, 0);
}
```

### 6. Forward Lookup (Index → Detail)

```c
TEST(index_event__forward_lookup__returns_detail_seq) {
    IndexEvent event = {
        .timestamp_ns = 1000,
        .function_id = 0x100000001,
        .thread_id = 1,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = 1,
        .detail_seq = 42
    };

    EXPECT_EQ(index_event_get_detail_seq(&event), 42);
}
```

### 7. Backward Lookup (Detail → Index)

```c
TEST(detail_event__backward_lookup__returns_index_seq) {
    DetailEventHeader header = {
        .total_length = sizeof(DetailEventHeader),
        .event_type = 3,
        .flags = 0,
        .index_seq = 17,
        .thread_id = 1,
        .timestamp = 1000
    };

    EXPECT_EQ(detail_event_get_index_seq(&header), 17);
}
```

## Integration Tests

### 1. Round-Trip with Rust Reader

```c
TEST(atf_writer__round_trip_rust__events_match) {
    // Write 1000 events
    AtfThreadWriter* writer = atf_writer_create("/tmp/test", 0, CLOCK_MACH_CONTINUOUS);
    for (int i = 0; i < 1000; i++) {
        atf_write_index_event(writer, i * 100, 0x100000001, EVENT_KIND_CALL, i % 10, UINT32_MAX);
    }
    atf_writer_finalize(writer);
    atf_writer_close(writer);

    // Read with Rust reader (via FFI or subprocess)
    RustAtfReader* reader = rust_atf_reader_open("/tmp/test/thread_0/index.atf");
    EXPECT_EQ(rust_atf_reader_event_count(reader), 1000);

    for (int i = 0; i < 1000; i++) {
        IndexEvent event;
        rust_atf_reader_get_event(reader, i, &event);
        EXPECT_EQ(event.timestamp_ns, i * 100);
    }
    rust_atf_reader_close(reader);
}
```

### 2. Index-Only Mode

```c
TEST(atf_writer__index_only__no_detail_file) {
    AtfThreadWriter* writer = atf_writer_create("/tmp/test", 0, CLOCK_MACH_CONTINUOUS);

    // Write without detail
    for (int i = 0; i < 100; i++) {
        atf_write_index_event(writer, i * 100, 0x100000001, EVENT_KIND_CALL, 0, UINT32_MAX);
    }
    atf_writer_finalize(writer);
    atf_writer_close(writer);

    // Verify no detail file
    EXPECT_FALSE(file_exists("/tmp/test/thread_0/detail.atf"));
    EXPECT_TRUE(file_exists("/tmp/test/thread_0/index.atf"));
}
```

### 3. Index + Detail Mode (Bidirectional)

```c
TEST(atf_writer__bidirectional__links_valid) {
    AtfThreadWriter* writer = atf_writer_create("/tmp/test", 0, CLOCK_MACH_CONTINUOUS);

    // Write paired events
    for (int i = 0; i < 100; i++) {
        uint32_t idx_seq, det_seq;
        atf_reserve_sequences(&writer->counters, true, &idx_seq, &det_seq);

        atf_write_index_event(writer, i * 100, 0x100000001, EVENT_KIND_CALL, 0, det_seq);
        atf_write_detail_event(writer, idx_seq, i * 100, &payload);
    }
    atf_writer_finalize(writer);
    atf_writer_close(writer);

    // Verify bidirectional links
    IndexReader* idx_reader = index_reader_open("/tmp/test/thread_0/index.atf");
    DetailReader* det_reader = detail_reader_open("/tmp/test/thread_0/detail.atf");

    for (int i = 0; i < 100; i++) {
        IndexEvent idx_event;
        index_reader_get(idx_reader, i, &idx_event);

        // Forward: index → detail
        DetailEventHeader det_event;
        detail_reader_get(det_reader, idx_event.detail_seq, &det_event);

        // Backward: detail → index
        EXPECT_EQ(det_event.index_seq, i);
    }
}
```

### 4. Crash Recovery

```c
TEST(atf_writer__crash_recovery__footer_authoritative) {
    AtfThreadWriter* writer = atf_writer_create("/tmp/test", 0, CLOCK_MACH_CONTINUOUS);

    // Write 100 events
    for (int i = 0; i < 100; i++) {
        atf_write_index_event(writer, i * 100, 0x100000001, EVENT_KIND_CALL, 0, UINT32_MAX);
    }
    atf_writer_finalize(writer);
    atf_writer_close(writer);

    // Corrupt header by changing event_count
    FILE* f = fopen("/tmp/test/thread_0/index.atf", "r+b");
    fseek(f, offsetof(AtfIndexHeader, event_count), SEEK_SET);
    uint32_t bad_count = 999;
    fwrite(&bad_count, sizeof(bad_count), 1, f);
    fclose(f);

    // Reader should use footer count
    IndexReader* reader = index_reader_open("/tmp/test/thread_0/index.atf");
    EXPECT_EQ(index_reader_event_count(reader), 100);  // From footer
}
```

## Performance Tests

### 1. Write Throughput

```c
TEST(atf_writer__throughput__exceeds_10M_per_sec) {
    AtfThreadWriter* writer = atf_writer_create("/tmp/bench", 0, CLOCK_MACH_CONTINUOUS);

    const int EVENT_COUNT = 10000000;  // 10M events

    uint64_t start = get_timestamp_ns();
    for (int i = 0; i < EVENT_COUNT; i++) {
        atf_write_index_event(writer, i, 0x100000001, EVENT_KIND_CALL, 0, UINT32_MAX);
    }
    atf_writer_finalize(writer);
    uint64_t end = get_timestamp_ns();

    atf_writer_close(writer);

    double elapsed_sec = (end - start) / 1e9;
    double events_per_sec = EVENT_COUNT / elapsed_sec;

    printf("Throughput: %.2f M events/sec\n", events_per_sec / 1e6);
    EXPECT_GT(events_per_sec, 10e6);  // > 10M events/sec
}
```

## Test Fixtures

### Session Directory Setup

```c
void setup_test_session() {
    system("rm -rf /tmp/test");
    mkdir("/tmp/test", 0755);
}

void teardown_test_session() {
    system("rm -rf /tmp/test");
}
```

## Success Criteria

1. All unit tests pass
2. All integration tests pass
3. Round-trip with Rust and Python readers succeeds
4. Write throughput exceeds 10M events/sec
5. Bidirectional linking verified in both directions
6. Crash recovery uses footer as authoritative source
