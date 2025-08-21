#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shared memory segment
typedef struct {
    void* address;
    size_t size;
    int fd;
    char name[256];
    bool is_creator;
} SharedMemory;

// Create or open shared memory
SharedMemory* shared_memory_create(const char* name, size_t size);
SharedMemory* shared_memory_open(const char* name, size_t size);

// Destroy shared memory
void shared_memory_destroy(SharedMemory* shm);

// Unlink shared memory (remove from system)
int shared_memory_unlink(const char* name);

#ifdef __cplusplus
}
#endif

#endif // SHARED_MEMORY_H