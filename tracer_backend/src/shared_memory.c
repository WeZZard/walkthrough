#include "shared_memory.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

SharedMemory* shared_memory_create(const char* name, size_t size) {
    SharedMemory* shm = calloc(1, sizeof(SharedMemory));
    if (!shm) return NULL;
    
    // Ensure name starts with /
    if (name[0] != '/') {
        snprintf(shm->name, sizeof(shm->name), "/%s", name);
    } else {
        strncpy(shm->name, name, sizeof(shm->name) - 1);
    }
    
    // Create shared memory object
    shm->fd = shm_open(shm->name, O_CREAT | O_RDWR, 0666);
    if (shm->fd == -1) {
        free(shm);
        return NULL;
    }
    
    // Set size
    if (ftruncate(shm->fd, size) == -1) {
        close(shm->fd);
        shm_unlink(shm->name);
        free(shm);
        return NULL;
    }
    
    // Map memory
    shm->address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->address == MAP_FAILED) {
        close(shm->fd);
        shm_unlink(shm->name);
        free(shm);
        return NULL;
    }
    
    shm->size = size;
    shm->is_creator = true;
    
    // Initialize to zero
    memset(shm->address, 0, size);
    
    return shm;
}

SharedMemory* shared_memory_open(const char* name, size_t size) {
    SharedMemory* shm = calloc(1, sizeof(SharedMemory));
    if (!shm) return NULL;
    
    // Ensure name starts with /
    if (name[0] != '/') {
        snprintf(shm->name, sizeof(shm->name), "/%s", name);
    } else {
        strncpy(shm->name, name, sizeof(shm->name) - 1);
    }
    
    // Open existing shared memory
    shm->fd = shm_open(shm->name, O_RDWR, 0666);
    if (shm->fd == -1) {
        free(shm);
        return NULL;
    }
    
    // Map memory
    shm->address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->address == MAP_FAILED) {
        close(shm->fd);
        free(shm);
        return NULL;
    }
    
    shm->size = size;
    shm->is_creator = false;
    
    return shm;
}

void shared_memory_destroy(SharedMemory* shm) {
    if (!shm) return;
    
    if (shm->address && shm->address != MAP_FAILED) {
        munmap(shm->address, shm->size);
    }
    
    if (shm->fd != -1) {
        close(shm->fd);
    }
    
    // Only unlink if we created it
    if (shm->is_creator) {
        shm_unlink(shm->name);
    }
    
    free(shm);
}

int shared_memory_unlink(const char* name) {
    char full_name[256];
    if (name[0] != '/') {
        snprintf(full_name, sizeof(full_name), "/%s", name);
        return shm_unlink(full_name);
    }
    return shm_unlink(name);
}