#ifndef RING_BUFFER_PRIVATE_H
#define RING_BUFFER_PRIVATE_H

#include <cstdint>
#include <cstring>

// We need to carefully handle the C/C++ compatibility
// RingBufferHeader is defined in tracer_types.h with _Atomic
// Do NOT include <atomic> as it conflicts with stdatomic.h
extern "C" {
#include <tracer_backend/utils/tracer_types.h>
}

#define RING_BUFFER_MAGIC 0xADA0
#define RING_BUFFER_VERSION 1

namespace ada {
namespace internal {

// ============================================================================
// RingBuffer Implementation Class
// ============================================================================

class RingBuffer {
public:
    // Constructor/destructor
    RingBuffer() = default;
    ~RingBuffer() = default;
    
    // Initialize as new ring buffer (creates header)
    bool initialize(void* memory, size_t size, size_t event_size) {
        if (!memory || size < sizeof(RingBufferHeader) + event_size) {
            return false;
        }
        
        header_ = static_cast<RingBufferHeader*>(memory);
        buffer_ = static_cast<uint8_t*>(memory) + sizeof(RingBufferHeader);
        event_size_ = event_size;
        buffer_size_ = size - sizeof(RingBufferHeader);
        
        // Initialize header
        header_->magic = RING_BUFFER_MAGIC;
        header_->version = RING_BUFFER_VERSION;
        header_->capacity = buffer_size_ / event_size;
        // Use C11 atomic operations on _Atomic members
        atomic_store_explicit(&header_->write_pos, 0, memory_order_relaxed);
        atomic_store_explicit(&header_->read_pos, 0, memory_order_relaxed);
        
        return true;
    }
    
    // Attach to existing ring buffer (doesn't create header)
    bool attach(void* memory, size_t size, size_t event_size) {
        if (!memory || size < sizeof(RingBufferHeader) + event_size) {
            return false;
        }
        
        header_ = static_cast<RingBufferHeader*>(memory);
        buffer_ = static_cast<uint8_t*>(memory) + sizeof(RingBufferHeader);
        event_size_ = event_size;
        buffer_size_ = size - sizeof(RingBufferHeader);
        
        // Verify magic number to ensure it's a valid ring buffer
        if (header_->magic != RING_BUFFER_MAGIC) {
            return false;
        }
        
        return true;
    }
    
    // Producer operations
    bool write(const void* event) {
        if (!event) return false;
        
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        uint32_t next_pos = (write_pos + 1) % header_->capacity;
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        
        // Check if full
        if (next_pos == read_pos) {
            return false; // Buffer full
        }
        
        // Copy event
        void* dest = buffer_ + (write_pos * event_size_);
        std::memcpy(dest, event, event_size_);
        
        // Update write position
        atomic_store_explicit(&header_->write_pos, next_pos, memory_order_release);
        
        return true;
    }
    
    size_t available_write() {
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        
        if (write_pos >= read_pos) {
            return header_->capacity - (write_pos - read_pos) - 1;
        } else {
            return read_pos - write_pos - 1;
        }
    }
    
    // Consumer operations
    bool read(void* event) {
        if (!event) return false;
        
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        
        // Check if empty
        if (read_pos == write_pos) {
            return false; // Buffer empty
        }
        
        // Copy event
        void* src = buffer_ + (read_pos * event_size_);
        std::memcpy(event, src, event_size_);
        
        // Update read position
        uint32_t next_pos = (read_pos + 1) % header_->capacity;
        atomic_store_explicit(&header_->read_pos, next_pos, memory_order_release);
        
        return true;
    }
    
    size_t read_batch(void* events, size_t max_count) {
        if (!events || max_count == 0) return 0;
        
        size_t count = 0;
        uint8_t* dest = static_cast<uint8_t*>(events);
        
        while (count < max_count) {
            if (!read(dest + (count * event_size_))) {
                break;
            }
            count++;
        }
        
        return count;
    }
    
    size_t available_read() {
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        
        if (write_pos >= read_pos) {
            return write_pos - read_pos;
        } else {
            return header_->capacity - (read_pos - write_pos);
        }
    }
    
    // Status operations
    bool is_empty() {
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        
        return write_pos == read_pos;
    }
    
    bool is_full() {
        uint32_t write_pos = atomic_load_explicit(&header_->write_pos, memory_order_acquire);
        uint32_t read_pos = atomic_load_explicit(&header_->read_pos, memory_order_acquire);
        
        uint32_t next_pos = (write_pos + 1) % header_->capacity;
        return next_pos == read_pos;
    }
    
    void reset() {
        atomic_store_explicit(&header_->write_pos, 0, memory_order_release);
        atomic_store_explicit(&header_->read_pos, 0, memory_order_release);
    }
    
    // Accessors for internal state
    size_t get_event_size() const { return event_size_; }
    size_t get_capacity() const { return header_ ? header_->capacity : 0; }
    RingBufferHeader* get_header() { return header_; }
    const RingBufferHeader* get_header() const { return header_; }
    
private:
    RingBufferHeader* header_{nullptr};
    uint8_t* buffer_{nullptr};
    size_t event_size_{0};
    size_t buffer_size_{0};
};

} // namespace internal
} // namespace ada

#endif // RING_BUFFER_PRIVATE_H