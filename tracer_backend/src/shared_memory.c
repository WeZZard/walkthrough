#include "shared_memory.h"
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>

typedef struct __SharedMemory {
    void* address;
    size_t size;
    int fd;
    char name[256];
    bool is_creator;
} __SharedMemory;

// Simple 32-bit FNV-1a hash for short role identifiers
static uint32_t shm_hash32(const char* s)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char* p = (const unsigned char*) s; *p; p++) {
        hash ^= (uint32_t) *p;
        hash *= 16777619u;
    }
    return hash;
}

// Typed constants (defined here; declared in header)
const char ADA_SHM_PREFIX[] = "ada";
const char ADA_ROLE_CONTROL[] = "control";
const char ADA_ROLE_INDEX[] = "index";
const char ADA_ROLE_DETAIL[] = "detail";

static pthread_once_t shm_sid_once = PTHREAD_ONCE_INIT;
static uint32_t shm_session_id = 0;

static void init_shm_session_id(void)
{
#ifdef __APPLE__
    uint32_t v = (uint32_t) arc4random();
#else
    uint32_t v = (uint32_t) rand();
#endif
    if (v == 0) v = 1u;
    shm_session_id = v;
}

uint32_t shared_memory_get_session_id(void)
{
    pthread_once(&shm_sid_once, init_shm_session_id);
    return shm_session_id;
}

uint32_t shared_memory_get_pid(void) {
    return (uint32_t) getpid();
}

static void shared_memory_build_name(char* dst, size_t dst_len, const char* role, pid_t pid, uint32_t session_id) {
    const char* env = getenv("ADA_SHM_DISABLE_UNIQUE");
    bool disable_unique = (env != NULL && env[0] != '\0' && env[0] != '0');
    if (disable_unique) {
        snprintf(dst, dst_len, "%s_%s", ADA_SHM_PREFIX, role);
#ifdef __APPLE__
        // POSIX shared memory names are limited (typically 31 chars on Darwin including the leading '/')
        // Our caller will prepend '/', so keep dst <= 30. If too long, fall back to hashed role.
        if (strlen(dst) > 30) {
            uint32_t rh = shm_hash32(role);
            snprintf(dst, dst_len, "%s_r%04x", ADA_SHM_PREFIX, (unsigned) (rh & 0xFFFFu));
        }
#endif
        return;
    }
    if (pid == 0) {
        pid = getpid();
    }
    if (session_id == 0) {
        fprintf(stderr, "Invalid session id: %u\n", session_id);
        return;
    }
    snprintf(dst, dst_len, "%s_%s_%d_%08x", ADA_SHM_PREFIX, role, (int) pid, (unsigned int) session_id);
#ifdef __APPLE__
    if (strlen(dst) > 30) {
        uint32_t rh = shm_hash32(role);
        // Use hashed role to stay within limits
        snprintf(dst, dst_len, "%s_r%04x_%d_%08x", ADA_SHM_PREFIX, (unsigned) (rh & 0xFFFFu), (int) pid, (unsigned int) session_id);
    }
#endif
}


static SharedMemoryRef shared_memory_create(const char* name, size_t size) {
    SharedMemoryRef shm = calloc(1, sizeof(__SharedMemory));
    if (!shm) {
        fprintf(stderr, "Failed to allocate memory for SharedMemory\n");
        return NULL;
    }
    
    // Ensure name starts with /
    if (name[0] != '/') {
        snprintf(shm->name, sizeof(shm->name), "/%s", name);
    } else {
        strncpy(shm->name, name, sizeof(shm->name) - 1);
    }
    
    // Create shared memory object
    shm->fd = shm_open(shm->name, O_CREAT | O_RDWR, 0666);
    if (shm->fd == -1) {
        fprintf(stderr, "Failed to create shared memory object: %s\n", strerror(errno));
        free(shm);
        return NULL;
    }
    
    // Set size
    if (ftruncate(shm->fd, size) == -1) {
        fprintf(stderr, "Failed to set size (%zu) of shared memory object: %s\n", size, strerror(errno));
        close(shm->fd);
        shm_unlink(shm->name);
        free(shm);
        return NULL;
    }
    
    // Map memory
    shm->address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->address == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory: %s\n", strerror(errno));
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

static SharedMemoryRef shared_memory_open(const char* name, size_t size) {
    SharedMemoryRef shm = calloc(1, sizeof(__SharedMemory));
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
        fprintf(stderr, "Failed to open shared memory object (%s): %s\n", shm->name, strerror(errno));
        free(shm);
        return NULL;
    }
    
    // Map memory
    shm->address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->address == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory: %s\n", strerror(errno));
        close(shm->fd);
        free(shm);
        return NULL;
    }
    
    shm->size = size;
    shm->is_creator = false;
    
    return shm;
}

SharedMemoryRef shared_memory_create_unique(const char* role, pid_t pid, uint32_t session_id,
                                          size_t size, char* out_name, size_t out_name_len) {
    char name[256];
    memset(name, 0, sizeof(name));
    shared_memory_build_name(name, sizeof(name), role, pid, session_id);
    if (strlen(name) == 0) {
        fprintf(stderr, "Failed to build name for shared memory\n");
        return NULL;
    }
    if (out_name && out_name_len > 0) {
        snprintf(out_name, out_name_len, "%s", name);
    }
    return shared_memory_create(name, size);
}

SharedMemoryRef shared_memory_open_unique(const char* role, pid_t pid, uint32_t session_id,
                                        size_t size) {
    char name[256];
    memset(name, 0, sizeof(name));
    shared_memory_build_name(name, sizeof(name), role, pid, session_id);
    if (strlen(name) == 0) {
        fprintf(stderr, "Failed to build name for shared memory\n");
        return NULL;
    }
    return shared_memory_open(name, size);
}

void shared_memory_destroy(SharedMemoryRef shm) {
    if (!shm) {
        fprintf(stderr, "SharedMemory is NULL\n");
        return;
    }
    
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

// MARK: - Accessing Shared Memory Properties

void* shared_memory_get_address(SharedMemoryRef shm) {
    return shm->address;
}

size_t shared_memory_get_size(SharedMemoryRef shm) {
    return shm->size;
}


int shared_memory_get_fd(SharedMemoryRef shm) {
    return shm->fd;
}


const char* shared_memory_get_name(SharedMemoryRef shm) {
    return shm->name;
}

bool shared_memory_is_creator(SharedMemoryRef shm) {
    return shm->is_creator;
}