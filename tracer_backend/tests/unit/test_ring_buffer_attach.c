#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include "ring_buffer.h"
#include "shared_memory.h"

void test_attach_preserves_data() {
    printf("Testing ring_buffer_attach preserves existing data...\n");
    
    // Create shared memory
    SharedMemory* shm = shared_memory_create("test_attach", 1024 * 1024);
    assert(shm != NULL);
    
    // Create and initialize a ring buffer
    RingBuffer* rb1 = ring_buffer_create(shm->address, shm->size, sizeof(int));
    assert(rb1 != NULL);
    
    // Write some data
    int test_values[] = {42, 84, 126, 168, 210};
    for (int i = 0; i < 5; i++) {
        bool result = ring_buffer_write(rb1, &test_values[i]);
        assert(result == true);
    }
    
    // Save the current positions
    uint32_t write_pos = atomic_load(&rb1->header->write_pos);
    uint32_t read_pos = atomic_load(&rb1->header->read_pos);
    printf("  After writing 5 items - write_pos: %u, read_pos: %u\n", write_pos, read_pos);
    
    // Now attach to the same memory (simulating agent attach)
    RingBuffer* rb2 = ring_buffer_attach(shm->address, shm->size, sizeof(int));
    assert(rb2 != NULL);
    
    // Check that positions are preserved
    uint32_t new_write_pos = atomic_load(&rb2->header->write_pos);
    uint32_t new_read_pos = atomic_load(&rb2->header->read_pos);
    printf("  After attach - write_pos: %u, read_pos: %u\n", new_write_pos, new_read_pos);
    
    assert(new_write_pos == write_pos);
    assert(new_read_pos == read_pos);
    
    // Verify we can read the data back
    for (int i = 0; i < 5; i++) {
        int value;
        bool result = ring_buffer_read(rb2, &value);
        assert(result == true);
        assert(value == test_values[i]);
        printf("  Read value %d: %d (expected %d)\n", i, value, test_values[i]);
    }
    
    // Clean up
    ring_buffer_destroy(rb1);
    ring_buffer_destroy(rb2);
    shared_memory_destroy(shm);
    
    printf("  ✓ Attach preserves data test passed\n");
}

void test_attach_fails_on_invalid_magic() {
    printf("Testing ring_buffer_attach fails on invalid magic...\n");
    
    // Create shared memory with garbage data
    SharedMemory* shm = shared_memory_create("test_bad_magic", 1024);
    assert(shm != NULL);
    
    // Fill with garbage
    memset(shm->address, 0xFF, shm->size);
    
    // Try to attach - should fail
    RingBuffer* rb = ring_buffer_attach(shm->address, shm->size, sizeof(int));
    assert(rb == NULL);
    
    shared_memory_destroy(shm);
    
    printf("  ✓ Invalid magic test passed\n");
}

void test_concurrent_attach_and_write() {
    printf("Testing concurrent attach and write...\n");
    
    // Create shared memory
    SharedMemory* shm = shared_memory_create("test_concurrent", 1024 * 1024);
    assert(shm != NULL);
    
    // Controller creates ring buffer
    RingBuffer* controller_rb = ring_buffer_create(shm->address, shm->size, sizeof(int));
    assert(controller_rb != NULL);
    
    // Controller writes some initial data
    for (int i = 0; i < 10; i++) {
        ring_buffer_write(controller_rb, &i);
    }
    
    // Agent attaches to existing ring buffer
    RingBuffer* agent_rb = ring_buffer_attach(shm->address, shm->size, sizeof(int));
    assert(agent_rb != NULL);
    
    // Both can write concurrently
    int controller_val = 1000;
    int agent_val = 2000;
    
    bool result1 = ring_buffer_write(controller_rb, &controller_val);
    bool result2 = ring_buffer_write(agent_rb, &agent_val);
    
    assert(result1 == true);
    assert(result2 == true);
    
    // Read all values to verify both writes succeeded
    int value;
    int count = 0;
    bool found_controller = false;
    bool found_agent = false;
    
    while (ring_buffer_read(controller_rb, &value)) {
        if (value == 1000) found_controller = true;
        if (value == 2000) found_agent = true;
        count++;
    }
    
    assert(found_controller == true);
    assert(found_agent == true);
    assert(count == 12); // 10 initial + 2 new
    
    printf("  Read %d values, found both writes\n", count);
    
    // Clean up
    ring_buffer_destroy(controller_rb);
    ring_buffer_destroy(agent_rb);
    shared_memory_destroy(shm);
    
    printf("  ✓ Concurrent attach and write test passed\n");
}

int main() {
    printf("=== Ring Buffer Attach Tests ===\n\n");
    
    test_attach_preserves_data();
    test_attach_fails_on_invalid_magic();
    test_concurrent_attach_and_write();
    
    printf("\n✅ All ring buffer attach tests passed!\n");
    return 0;
}