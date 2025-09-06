#include <tracer_backend/utils/ring_buffer.h>
#include "ring_buffer_private.h"
#include <cstdlib>

// ============================================================================
// C API Implementation (extern "C")
// ============================================================================

extern "C" {

// Create ring buffer (initializes header)
RingBuffer* ring_buffer_create(void* memory, size_t size, size_t event_size) {
    auto* rb = new ada::internal::RingBuffer();
    if (!rb) {
        return nullptr;
    }
    
    if (!rb->initialize(memory, size, event_size)) {
        delete rb;
        return nullptr;
    }
    
    return reinterpret_cast<RingBuffer*>(rb);
}

// Attach to existing ring buffer (does not initialize header)
RingBuffer* ring_buffer_attach(void* memory, size_t size, size_t event_size) {
    auto* rb = new ada::internal::RingBuffer();
    if (!rb) {
        return nullptr;
    }
    
    if (!rb->attach(memory, size, event_size)) {
        delete rb;
        return nullptr;
    }
    
    return reinterpret_cast<RingBuffer*>(rb);
}

// Producer operations
bool ring_buffer_write(RingBuffer* rb, const void* event) {
    if (!rb) return false;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->write(event);
}

size_t ring_buffer_available_write(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->available_write();
}

// Consumer operations
bool ring_buffer_read(RingBuffer* rb, void* event) {
    if (!rb) return false;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->read(event);
}

size_t ring_buffer_available_read(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->available_read();
}

size_t ring_buffer_read_batch(RingBuffer* rb, void* events, size_t max_count) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->read_batch(events, max_count);
}

// Status
bool ring_buffer_is_empty(RingBuffer* rb) {
    if (!rb) return true;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->is_empty();
}

bool ring_buffer_is_full(RingBuffer* rb) {
    if (!rb) return true;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->is_full();
}

void ring_buffer_reset(RingBuffer* rb) {
    if (!rb) return;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    impl->reset();
}

// Cleanup
void ring_buffer_destroy(RingBuffer* rb) {
    if (rb) {
        auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
        delete impl;
    }
}

// New accessor functions
size_t ring_buffer_get_event_size(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_event_size();
}

size_t ring_buffer_get_capacity(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_capacity();
}

RingBufferHeader* ring_buffer_get_header(RingBuffer* rb) {
    if (!rb) return nullptr;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_header();
}

} // extern "C"

// Additional C API (not in extern "C" block above to maintain grouping)
extern "C" {

uint64_t ring_buffer_get_overflow_count(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    auto* hdr = impl->get_header();
    if (!hdr) return 0;
    return __atomic_load_n(&hdr->overflow_count, __ATOMIC_ACQUIRE);
}

}
