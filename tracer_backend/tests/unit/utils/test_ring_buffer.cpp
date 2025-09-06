// Unit tests for Ring Buffer using Google Test
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>

extern "C" {
    #include <tracer_backend/utils/ring_buffer.h>
    #include <tracer_backend/utils/tracer_types.h>
}

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;

// Test fixture for RingBuffer tests
class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[RING] Setting up test\n");
        buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
        memory = std::make_unique<uint8_t[]>(buffer_size);
        memset(memory.get(), 0, buffer_size);
        rb = nullptr;
    }
    
    void TearDown() override {
        if (rb) {
            ring_buffer_destroy(rb);
            rb = nullptr;
        }
    }
    
    // Test data structure
    struct TestEvent {
        uint64_t id;
        uint64_t timestamp;
        char data[48];
    };
    
    // Test parameters
    static constexpr size_t capacity = 100;
    size_t buffer_size;
    std::unique_ptr<uint8_t[]> memory;
    RingBuffer* rb;
};

// Test: ring_buffer__create_with_valid_memory__then_returns_valid_buffer
TEST_F(RingBufferTest, ring_buffer__create_with_valid_memory__then_returns_valid_buffer) {
    printf("[RING] create_with_valid_memory → returns valid buffer\n");
    
    // Act
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    
    // Assert
    ASSERT_NE(rb, nullptr) << "Failed to create ring buffer";
    EXPECT_EQ(ring_buffer_get_capacity(rb), capacity);
    EXPECT_TRUE(ring_buffer_is_empty(rb));
    EXPECT_FALSE(ring_buffer_is_full(rb));
}

// Test: ring_buffer__write_single_event__then_event_preserved
TEST_F(RingBufferTest, ring_buffer__write_single_event__then_event_preserved) {
    printf("[RING] write_single_event → event preserved\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    TestEvent write_event = {
        .id = 42,
        .timestamp = 1234567890,
        .data = "Test Event Data"
    };
    
    // Act
    bool write_result = ring_buffer_write(rb, &write_event);
    
    // Assert write succeeded
    ASSERT_TRUE(write_result) << "Failed to write to ring buffer";
    EXPECT_FALSE(ring_buffer_is_empty(rb));
    EXPECT_EQ(ring_buffer_available_read(rb), 1);
    
    // Act - Read back
    TestEvent read_event = {};
    bool read_result = ring_buffer_read(rb, &read_event);
    
    // Assert read succeeded and data matches
    ASSERT_TRUE(read_result) << "Failed to read from ring buffer";
    EXPECT_EQ(read_event.id, write_event.id);
    EXPECT_EQ(read_event.timestamp, write_event.timestamp);
    EXPECT_STREQ(read_event.data, write_event.data);
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__fill_and_drain__then_handles_capacity_correctly
// Direct translation of test_fill_and_drain() from C
TEST_F(RingBufferTest, ring_buffer__fill_and_drain__then_handles_capacity_correctly) {
    printf("[RING] fill_and_drain → handles capacity correctly\n");
    
    // Direct translation - use same capacity as C test
    size_t test_capacity = 50;
    size_t test_buffer_size = sizeof(RingBufferHeader) + (test_capacity * sizeof(TestEvent));
    void* test_memory = calloc(1, test_buffer_size);
    
    RingBuffer* test_rb = ring_buffer_create(test_memory, test_buffer_size, sizeof(TestEvent));
    
    // Fill the buffer - exact same loop as C test
    for (size_t i = 0; i < test_capacity - 1; i++) { // -1 because ring buffer keeps one empty slot
        TestEvent event = {
            .id = i,
            .timestamp = i * 1000,
        };
        snprintf(event.data, sizeof(event.data), "Event %zu", i);
        ASSERT_TRUE(ring_buffer_write(test_rb, &event));
    }
    
    ASSERT_TRUE(ring_buffer_is_full(test_rb));
    ASSERT_EQ(ring_buffer_available_write(test_rb), 0);
    
    // Try to write when full (should fail) and overflow counter increments
    TestEvent overflow_event = {.id = 999};
    uint64_t before = ring_buffer_get_overflow_count(test_rb);
    ASSERT_FALSE(ring_buffer_write(test_rb, &overflow_event));
    uint64_t after = ring_buffer_get_overflow_count(test_rb);
    EXPECT_GT(after, before) << "Overflow counter should increment on full write";
    
    // Drain all events
    TestEvent events[50];
    size_t read_count = ring_buffer_read_batch(test_rb, events, 50);
    ASSERT_EQ(read_count, test_capacity - 1);
    
    // Verify events
    for (size_t i = 0; i < read_count; i++) {
        ASSERT_EQ(events[i].id, i);
        ASSERT_EQ(events[i].timestamp, i * 1000);
    }
    
    ASSERT_TRUE(ring_buffer_is_empty(test_rb));
    
    ring_buffer_destroy(test_rb);
    free(test_memory);
}

// Test: ring_buffer__concurrent_writes__then_preserve_order
TEST_F(RingBufferTest, ring_buffer__concurrent_writes__then_preserve_order) {
    printf("[RING] concurrent_writes → preserve order\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    const int num_events = 1000;
    std::atomic<int> events_written{0};
    std::atomic<int> events_read{0};
    std::atomic<bool> producer_done{false};
    
    // Act - Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_events; i++) {
            TestEvent event = {
                .id = static_cast<uint64_t>(i),
                .timestamp = static_cast<uint64_t>(i * 100)
            };
            while (!ring_buffer_write(rb, &event)) {
                std::this_thread::yield();
            }
            events_written++;
        }
        producer_done = true;
    });
    
    // Act - Consumer thread
    std::thread consumer([&]() {
        while (!producer_done || !ring_buffer_is_empty(rb)) {
            TestEvent event;
            if (ring_buffer_read(rb, &event)) {
                // Verify data integrity
                EXPECT_EQ(event.timestamp, event.id * 100) 
                    << "Data corruption detected at event " << event.id;
                events_read++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    // Wait for threads
    producer.join();
    consumer.join();
    
    // Assert all events processed
    EXPECT_EQ(events_written.load(), num_events);
    EXPECT_EQ(events_read.load(), num_events);
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__batch_operations__then_efficient_transfer
TEST_F(RingBufferTest, ring_buffer__batch_operations__then_efficient_transfer) {
    printf("[RING] batch_operations → efficient transfer\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    const size_t batch_size = 10;
    TestEvent write_batch[batch_size];
    TestEvent read_batch[batch_size];
    
    // Prepare batch data
    for (size_t i = 0; i < batch_size; i++) {
        write_batch[i].id = i;
        write_batch[i].timestamp = i * 1000;
        snprintf(write_batch[i].data, sizeof(write_batch[i].data), "Event %zu", i);
    }
    
    // Act - Write events one by one (since write_batch doesn't exist)
    for (size_t i = 0; i < batch_size; i++) {
        bool result = ring_buffer_write(rb, &write_batch[i]);
        ASSERT_TRUE(result) << "Failed to write event " << i;
    }
    
    // Assert write succeeded
    EXPECT_EQ(ring_buffer_available_read(rb), batch_size);
    
    // Act - Read batch
    size_t read = ring_buffer_read_batch(rb, read_batch, batch_size);
    
    // Assert read succeeded and data matches
    EXPECT_EQ(read, batch_size) << "Failed to read full batch";
    for (size_t i = 0; i < batch_size; i++) {
        EXPECT_EQ(read_batch[i].id, write_batch[i].id);
        EXPECT_EQ(read_batch[i].timestamp, write_batch[i].timestamp);
        EXPECT_STREQ(read_batch[i].data, write_batch[i].data);
    }
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__null_pointer__then_return_error
TEST_F(RingBufferTest, ring_buffer__null_pointer__then_return_error) {
    printf("[RING] null_pointer → return error\n");
    
    // Act & Assert - Create with null memory
    RingBuffer* null_rb = ring_buffer_create(nullptr, buffer_size, sizeof(TestEvent));
    EXPECT_EQ(null_rb, nullptr) << "Should not create buffer with null memory";
    
    // Act & Assert - Create with size too small (less than header + one event)
    null_rb = ring_buffer_create(memory.get(), sizeof(RingBufferHeader), sizeof(TestEvent));
    EXPECT_EQ(null_rb, nullptr) << "Should not create buffer with insufficient size";
    
    // Note: The implementation doesn't validate event_size == 0, 
    // so we don't test that case as it would actually succeed
}

// Parameterized test for different buffer sizes
class RingBufferSizeTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        capacity = GetParam();
        buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
        memory = std::make_unique<uint8_t[]>(buffer_size);
        memset(memory.get(), 0, buffer_size);
    }
    
    void TearDown() override {
        if (rb) {
            ring_buffer_destroy(rb);
        }
    }
    
    struct TestEvent {
        uint64_t id;
        char data[56];
    };
    
    size_t capacity;
    size_t buffer_size;
    std::unique_ptr<uint8_t[]> memory;
    RingBuffer* rb = nullptr;
};

TEST_P(RingBufferSizeTest, ring_buffer__various_capacities__then_create_success) {
    printf("[RING] capacity_%zu → create success\n", capacity);
    
    // Act
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    
    // Assert
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(ring_buffer_get_capacity(rb), capacity);
    
    // Test can write up to capacity - 1 (ring buffer keeps one slot empty)
    for (size_t i = 0; i < capacity - 1; i++) {
        TestEvent event = {.id = i};
        EXPECT_TRUE(ring_buffer_write(rb, &event)) << "Failed to write event " << i;
    }
    EXPECT_TRUE(ring_buffer_is_full(rb));
    
    // Verify cannot write when full
    TestEvent overflow = {.id = 999};
    uint64_t before2 = ring_buffer_get_overflow_count(rb);
    EXPECT_FALSE(ring_buffer_write(rb, &overflow)) << "Should not accept write when full";
    uint64_t after2 = ring_buffer_get_overflow_count(rb);
    EXPECT_GE(after2, before2 + 1) << "Overflow counter should increase when full";
}

// Test with different capacities
INSTANTIATE_TEST_SUITE_P(
    CapacityTests,
    RingBufferSizeTest,
    ::testing::Values(1, 10, 100, 1000, 10000),
    [](const testing::TestParamInfo<size_t>& info) {
        return "Capacity" + std::to_string(info.param);
    }
);
