#include "ring_buffer.h"
#include "shared_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

typedef struct {
    uint64_t id;
    uint64_t timestamp;
    char data[48];
} TestEvent;

void test_basic_operations() {
    printf("Testing basic ring buffer operations...\n");
    
    // Allocate memory for ring buffer
    size_t buffer_size = sizeof(RingBufferHeader) + (100 * sizeof(TestEvent));
    void* memory = calloc(1, buffer_size);
    
    // Create ring buffer
    RingBuffer* rb = ring_buffer_create(memory, buffer_size, sizeof(TestEvent));
    assert(rb != NULL);
    assert(rb->header->capacity == 100);
    assert(ring_buffer_is_empty(rb));
    assert(!ring_buffer_is_full(rb));
    
    // Write an event
    TestEvent write_event = {
        .id = 1,
        .timestamp = 12345,
    };
    strcpy(write_event.data, "Test Event 1");
    
    assert(ring_buffer_write(rb, &write_event));
    assert(!ring_buffer_is_empty(rb));
    assert(ring_buffer_available_read(rb) == 1);
    
    // Read the event
    TestEvent read_event;
    assert(ring_buffer_read(rb, &read_event));
    assert(read_event.id == 1);
    assert(read_event.timestamp == 12345);
    assert(strcmp(read_event.data, "Test Event 1") == 0);
    
    assert(ring_buffer_is_empty(rb));
    
    ring_buffer_destroy(rb);
    free(memory);
    
    printf("  ✓ Basic operations test passed\n");
}

void test_fill_and_drain() {
    printf("Testing fill and drain...\n");
    
    size_t capacity = 50;
    size_t buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
    void* memory = calloc(1, buffer_size);
    
    RingBuffer* rb = ring_buffer_create(memory, buffer_size, sizeof(TestEvent));
    
    // Fill the buffer
    for (size_t i = 0; i < capacity - 1; i++) { // -1 because ring buffer keeps one empty slot
        TestEvent event = {
            .id = i,
            .timestamp = i * 1000,
        };
        sprintf(event.data, "Event %zu", i);
        assert(ring_buffer_write(rb, &event));
    }
    
    assert(ring_buffer_is_full(rb));
    assert(ring_buffer_available_write(rb) == 0);
    
    // Try to write when full (should fail)
    TestEvent overflow_event = {.id = 999};
    assert(!ring_buffer_write(rb, &overflow_event));
    
    // Drain all events
    TestEvent events[50];
    size_t read_count = ring_buffer_read_batch(rb, events, 50);
    assert(read_count == capacity - 1);
    
    // Verify events
    for (size_t i = 0; i < read_count; i++) {
        assert(events[i].id == i);
        assert(events[i].timestamp == i * 1000);
    }
    
    assert(ring_buffer_is_empty(rb));
    
    ring_buffer_destroy(rb);
    free(memory);
    
    printf("  ✓ Fill and drain test passed\n");
}

void test_wrap_around() {
    printf("Testing wrap around...\n");
    
    size_t capacity = 10;
    size_t buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
    void* memory = calloc(1, buffer_size);
    
    RingBuffer* rb = ring_buffer_create(memory, buffer_size, sizeof(TestEvent));
    
    // Write 5 events
    for (int i = 0; i < 5; i++) {
        TestEvent event = {.id = i};
        assert(ring_buffer_write(rb, &event));
    }
    
    // Read 3 events
    TestEvent event;
    for (int i = 0; i < 3; i++) {
        assert(ring_buffer_read(rb, &event));
        assert(event.id == i);
    }
    
    // Write 7 more events (will wrap around)
    for (int i = 5; i < 12; i++) {
        TestEvent event = {.id = i};
        assert(ring_buffer_write(rb, &event));
    }
    
    // Read remaining events in order
    int expected_id = 3; // We already read 0, 1, 2
    while (!ring_buffer_is_empty(rb)) {
        assert(ring_buffer_read(rb, &event));
        if (expected_id < 5) {
            assert(event.id == expected_id);
        } else {
            assert(event.id == expected_id);
        }
        expected_id++;
    }
    
    assert(expected_id == 12);
    
    ring_buffer_destroy(rb);
    free(memory);
    
    printf("  ✓ Wrap around test passed\n");
}

// Producer-consumer test structures
typedef struct {
    RingBuffer* rb;
    int num_events;
    bool* running;
} ThreadData;

void* producer_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    for (int i = 0; i < data->num_events; i++) {
        TestEvent event = {
            .id = i,
            .timestamp = i * 100,
        };
        sprintf(event.data, "Producer event %d", i);
        
        // Retry if buffer is full
        while (!ring_buffer_write(data->rb, &event)) {
            usleep(100); // Wait 100us
        }
    }
    
    return NULL;
}

void* consumer_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    int consumed = 0;
    
    while (*data->running || !ring_buffer_is_empty(data->rb)) {
        TestEvent events[10];
        size_t count = ring_buffer_read_batch(data->rb, events, 10);
        consumed += count;
        
        if (count == 0) {
            usleep(100); // Wait 100us if nothing to read
        }
    }
    
    return (void*)(intptr_t)consumed;
}

void test_concurrent_access() {
    printf("Testing concurrent producer-consumer...\n");
    
    // Use shared memory for true concurrent test
    SharedMemory* shm = shared_memory_create("test_concurrent", 64 * 1024);
    assert(shm != NULL);
    
    RingBuffer* rb = ring_buffer_create(shm->address, shm->size, sizeof(TestEvent));
    assert(rb != NULL);
    
    bool running = true;
    ThreadData producer_data = {
        .rb = rb,
        .num_events = 10000,
        .running = &running
    };
    
    ThreadData consumer_data = {
        .rb = rb,
        .num_events = 10000,
        .running = &running
    };
    
    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_thread, &producer_data);
    pthread_create(&consumer, NULL, consumer_thread, &consumer_data);
    
    // Wait for producer to finish
    pthread_join(producer, NULL);
    
    // Let consumer drain remaining events
    running = false;
    
    void* result;
    pthread_join(consumer, &result);
    int consumed = (int)(intptr_t)result;
    
    assert(consumed == 10000);
    assert(ring_buffer_is_empty(rb));
    
    ring_buffer_destroy(rb);
    shared_memory_destroy(shm);
    
    printf("  ✓ Concurrent access test passed (10000 events)\n");
}

void test_reset() {
    printf("Testing reset functionality...\n");
    
    size_t buffer_size = sizeof(RingBufferHeader) + (10 * sizeof(TestEvent));
    void* memory = calloc(1, buffer_size);
    
    RingBuffer* rb = ring_buffer_create(memory, buffer_size, sizeof(TestEvent));
    
    // Write some events
    for (int i = 0; i < 5; i++) {
        TestEvent event = {.id = i};
        assert(ring_buffer_write(rb, &event));
    }
    
    assert(ring_buffer_available_read(rb) == 5);
    
    // Reset the buffer
    ring_buffer_reset(rb);
    
    assert(ring_buffer_is_empty(rb));
    assert(ring_buffer_available_read(rb) == 0);
    assert(rb->header->write_pos == 0);
    assert(rb->header->read_pos == 0);
    
    // Should be able to write again
    TestEvent event = {.id = 100};
    assert(ring_buffer_write(rb, &event));
    assert(ring_buffer_available_read(rb) == 1);
    
    ring_buffer_destroy(rb);
    free(memory);
    
    printf("  ✓ Reset test passed\n");
}

int main() {
    printf("\n=== Ring Buffer Unit Tests ===\n\n");
    
    test_basic_operations();
    test_fill_and_drain();
    test_wrap_around();
    test_concurrent_access();
    test_reset();
    
    printf("\n✅ All ring buffer tests passed!\n\n");
    return 0;
}