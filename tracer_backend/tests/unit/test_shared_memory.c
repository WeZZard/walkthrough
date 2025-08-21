#include "shared_memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_create_and_destroy() {
    printf("Testing shared memory create and destroy...\n");
    
    SharedMemory* shm = shared_memory_create("test_shm", 4096);
    assert(shm != NULL);
    assert(shm->size == 4096);
    assert(shm->address != NULL);
    assert(shm->is_creator == true);
    
    // Write some data
    strcpy(shm->address, "Hello, Shared Memory!");
    
    // Cleanup
    shared_memory_destroy(shm);
    
    printf("  ✓ Create and destroy test passed\n");
}

void test_open_existing() {
    printf("Testing opening existing shared memory...\n");
    
    // Create shared memory
    SharedMemory* shm1 = shared_memory_create("test_open", 4096);
    assert(shm1 != NULL);
    
    // Write data
    strcpy(shm1->address, "Test Data");
    
    // Open the same shared memory from another handle
    SharedMemory* shm2 = shared_memory_open("test_open", 4096);
    assert(shm2 != NULL);
    assert(shm2->is_creator == false);
    
    // Verify data is visible
    assert(strcmp(shm2->address, "Test Data") == 0);
    
    // Modify data from second handle
    strcpy(shm2->address, "Modified Data");
    
    // Verify modification is visible from first handle
    assert(strcmp(shm1->address, "Modified Data") == 0);
    
    // Cleanup
    shared_memory_destroy(shm2);
    shared_memory_destroy(shm1);
    
    printf("  ✓ Open existing test passed\n");
}

void test_multiple_regions() {
    printf("Testing multiple shared memory regions...\n");
    
    SharedMemory* shm1 = shared_memory_create("region1", 1024);
    SharedMemory* shm2 = shared_memory_create("region2", 2048);
    SharedMemory* shm3 = shared_memory_create("region3", 4096);
    
    assert(shm1 != NULL);
    assert(shm2 != NULL);
    assert(shm3 != NULL);
    
    // Write different data to each
    strcpy(shm1->address, "Region 1");
    strcpy(shm2->address, "Region 2");
    strcpy(shm3->address, "Region 3");
    
    // Verify independence
    assert(strcmp(shm1->address, "Region 1") == 0);
    assert(strcmp(shm2->address, "Region 2") == 0);
    assert(strcmp(shm3->address, "Region 3") == 0);
    
    // Cleanup
    shared_memory_destroy(shm1);
    shared_memory_destroy(shm2);
    shared_memory_destroy(shm3);
    
    printf("  ✓ Multiple regions test passed\n");
}

void test_large_allocation() {
    printf("Testing large allocation...\n");
    
    size_t size = 32 * 1024 * 1024; // 32MB
    SharedMemory* shm = shared_memory_create("large_shm", size);
    assert(shm != NULL);
    assert(shm->size == size);
    
    // Write pattern at beginning and end
    memset(shm->address, 0xAA, 1024);
    memset((char*)shm->address + size - 1024, 0xBB, 1024);
    
    // Verify pattern
    unsigned char* data = (unsigned char*)shm->address;
    assert(data[0] == 0xAA);
    assert(data[1023] == 0xAA);
    assert(data[size - 1024] == 0xBB);
    assert(data[size - 1] == 0xBB);
    
    shared_memory_destroy(shm);
    
    printf("  ✓ Large allocation test passed\n");
}

int main() {
    printf("\n=== Shared Memory Unit Tests ===\n\n");
    
    test_create_and_destroy();
    test_open_existing();
    test_multiple_regions();
    test_large_allocation();
    
    printf("\n✅ All shared memory tests passed!\n\n");
    return 0;
}