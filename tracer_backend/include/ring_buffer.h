#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "tracer_types.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lock-free SPSC ring buffer
struct RingBuffer {
    RingBufferHeader* header;
    void* buffer;
    // Each event is a fixed size
    size_t event_size;
    size_t buffer_size;
};
typedef struct RingBuffer RingBuffer;

// Create ring buffer (initializes header)
RingBuffer* ring_buffer_create(void* memory, size_t size, size_t event_size);

// Attach to existing ring buffer (does not initialize header)
RingBuffer* ring_buffer_attach(void* memory, size_t size, size_t event_size);

// Producer operations
bool ring_buffer_write(RingBuffer* rb, const void* event);
size_t ring_buffer_available_write(RingBuffer* rb);

// Consumer operations
bool ring_buffer_read(RingBuffer* rb, void* event);
size_t ring_buffer_available_read(RingBuffer* rb);
size_t ring_buffer_read_batch(RingBuffer* rb, void* events, size_t max_count);

// Status
bool ring_buffer_is_empty(RingBuffer* rb);
bool ring_buffer_is_full(RingBuffer* rb);
void ring_buffer_reset(RingBuffer* rb);

// Cleanup
void ring_buffer_destroy(RingBuffer* rb);

#ifdef __cplusplus
}
#endif

#endif // RING_BUFFER_H