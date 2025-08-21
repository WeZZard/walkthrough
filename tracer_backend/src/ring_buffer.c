#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define RING_BUFFER_MAGIC 0xADA0
#define RING_BUFFER_VERSION 1

RingBuffer* ring_buffer_create(void* memory, size_t size, size_t event_size) {
    if (!memory || size < sizeof(RingBufferHeader) + event_size) {
        return NULL;
    }
    
    RingBuffer* rb = calloc(1, sizeof(RingBuffer));
    if (!rb) return NULL;
    
    rb->header = (RingBufferHeader*)memory;
    rb->buffer = (uint8_t*)memory + sizeof(RingBufferHeader);
    rb->event_size = event_size;
    rb->buffer_size = size - sizeof(RingBufferHeader);
    
    // Initialize header
    rb->header->magic = RING_BUFFER_MAGIC;
    rb->header->version = RING_BUFFER_VERSION;
    rb->header->capacity = rb->buffer_size / event_size;
    atomic_store_explicit(&rb->header->write_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->header->read_pos, 0, memory_order_relaxed);
    
    return rb;
}

RingBuffer* ring_buffer_attach(void* memory, size_t size, size_t event_size) {
    if (!memory || size < sizeof(RingBufferHeader) + event_size) {
        return NULL;
    }
    
    RingBuffer* rb = calloc(1, sizeof(RingBuffer));
    if (!rb) return NULL;
    
    rb->header = (RingBufferHeader*)memory;
    rb->buffer = (uint8_t*)memory + sizeof(RingBufferHeader);
    rb->event_size = event_size;
    rb->buffer_size = size - sizeof(RingBufferHeader);
    
    // Do NOT initialize header - just attach to existing
    // Verify magic number to ensure it's a valid ring buffer
    if (rb->header->magic != RING_BUFFER_MAGIC) {
        free(rb);
        return NULL;
    }
    
    return rb;
}

bool ring_buffer_write(RingBuffer* rb, const void* event) {
    if (!rb || !event) return false;
    
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    uint32_t next_pos = (write_pos + 1) % rb->header->capacity;
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    
    // Check if full
    if (next_pos == read_pos) {
        return false; // Buffer full
    }
    
    // Copy event
    void* dest = (uint8_t*)rb->buffer + (write_pos * rb->event_size);
    memcpy(dest, event, rb->event_size);
    
    // Update write position
    atomic_store_explicit(&rb->header->write_pos, next_pos, memory_order_release);
    
    return true;
}

bool ring_buffer_read(RingBuffer* rb, void* event) {
    if (!rb || !event) return false;
    
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    
    // Check if empty
    if (read_pos == write_pos) {
        return false; // Buffer empty
    }
    
    // Copy event
    void* src = (uint8_t*)rb->buffer + (read_pos * rb->event_size);
    memcpy(event, src, rb->event_size);
    
    // Update read position
    uint32_t next_pos = (read_pos + 1) % rb->header->capacity;
    atomic_store_explicit(&rb->header->read_pos, next_pos, memory_order_release);
    
    return true;
}

size_t ring_buffer_read_batch(RingBuffer* rb, void* events, size_t max_count) {
    if (!rb || !events || max_count == 0) return 0;
    
    size_t count = 0;
    uint8_t* dest = (uint8_t*)events;
    
    while (count < max_count) {
        if (!ring_buffer_read(rb, dest + (count * rb->event_size))) {
            break;
        }
        count++;
    }
    
    return count;
}

size_t ring_buffer_available_write(RingBuffer* rb) {
    if (!rb) return 0;
    
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    
    if (write_pos >= read_pos) {
        return rb->header->capacity - (write_pos - read_pos) - 1;
    } else {
        return read_pos - write_pos - 1;
    }
}

size_t ring_buffer_available_read(RingBuffer* rb) {
    if (!rb) return 0;
    
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return rb->header->capacity - (read_pos - write_pos);
    }
}

bool ring_buffer_is_empty(RingBuffer* rb) {
    if (!rb) return true;
    
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    
    return write_pos == read_pos;
}

bool ring_buffer_is_full(RingBuffer* rb) {
    if (!rb) return true;
    
    uint32_t write_pos = atomic_load_explicit(&rb->header->write_pos, memory_order_acquire);
    uint32_t read_pos = atomic_load_explicit(&rb->header->read_pos, memory_order_acquire);
    
    uint32_t next_pos = (write_pos + 1) % rb->header->capacity;
    return next_pos == read_pos;
}

void ring_buffer_reset(RingBuffer* rb) {
    if (!rb) return;
    
    atomic_store_explicit(&rb->header->write_pos, 0, memory_order_release);
    atomic_store_explicit(&rb->header->read_pos, 0, memory_order_release);
}

void ring_buffer_destroy(RingBuffer* rb) {
    free(rb);
}