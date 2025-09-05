#ifndef THREAD_REGISTRY_PRIVATE_H
#define THREAD_REGISTRY_PRIVATE_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/ring_buffer.h>
// Need private definitions for concrete implementation
#include "tracer_types_private.h"

// Optional logging flag defined in C shim (thread_registry.cpp)
extern bool needs_log_thread_registry_registry;

namespace ada {
namespace internal {

// ============================================================================
// Structured Lane Memory Layout (debugger-friendly)
// ============================================================================

// Memory layout for a single lane - EXPLICIT structure, not pointer arithmetic
struct LaneMemoryLayout {
    // Ring descriptor with multi-segment support (id + offset)
    struct RingDescriptor {
        uint32_t segment_id;   // SHM segment identifier (0 = local segment)
        uint32_t bytes;        // Size of the ring buffer in bytes
        uint64_t offset;       // Byte offset from segment base
    };
    // Ring buffer pointers array (just void* to memory regions)
    void* ring_ptrs[RINGS_PER_INDEX_LANE];
    // RingBuffer handles bound to ring_ptrs (attached)
    ::RingBuffer* rb_handles[RINGS_PER_INDEX_LANE]{};
    
    // Pointer to ring buffer memory (allocated separately)
    void* ring_memory_base;
    size_t ring_memory_size;
    
    // SPSC queues - explicit arrays
    alignas(CACHE_LINE_SIZE)
    uint32_t submit_queue[QUEUE_COUNT_INDEX_LANE];
    
    alignas(CACHE_LINE_SIZE)
    uint32_t free_queue[QUEUE_COUNT_INDEX_LANE];
    
    // Debug helper: print layout info
    void debug_print() const {
        printf("LaneMemoryLayout at %p:\n", this);
        printf("  ring_ptrs:    %p - %p\n", ring_ptrs, ring_ptrs + RINGS_PER_INDEX_LANE);
        printf("  ring_descs[0]: seg=%u off=%llu bytes=%u\n",
               ring_descs[0].segment_id,
               (unsigned long long)ring_descs[0].offset,
               ring_descs[0].bytes);
        printf("  ring_memory:  %p (size=%zu)\n", ring_memory_base, ring_memory_size);
        printf("  submit_queue: %p - %p\n", submit_queue, submit_queue + QUEUE_COUNT_INDEX_LANE);
        printf("  free_queue:   %p - %p\n", free_queue, free_queue + QUEUE_COUNT_INDEX_LANE);
    }
    
    // Descriptors for rings (parallel to ring_ptrs). For now we fill for the active lane count.
    RingDescriptor ring_descs[RINGS_PER_INDEX_LANE];
};

// Segment info for multi-segment design
enum : uint8_t {
    SEGMENT_KIND_INDEX   = 1,
    SEGMENT_KIND_DETAIL  = 2,
    SEGMENT_KIND_OVERFLOW = 3,
};

struct SegmentInfo {
    uint32_t id;           // Unique id (>=1)
    uint8_t  kind;         // Index/Detail/Overflow
    uint8_t  flags;        // Reserved
    uint16_t _pad{0};
    uint64_t size;         // Bytes in segment
    uint64_t base_offset;  // Offset from registry base if local
    std::atomic<uint64_t> used{0}; // Bump allocator head (bytes)
    char     name[64];     // Optional OS name
};

// ============================================================================
// Enhanced Lane with proper C++ abstractions
// ============================================================================

class Lane {
public:
    // Ring pool management
    std::atomic<uint32_t> active_idx{0};
    uint32_t ring_count{0};
    // Queue capacities (modulo bases)
    uint32_t submit_capacity{QUEUE_COUNT_INDEX_LANE};
    uint32_t free_capacity{QUEUE_COUNT_INDEX_LANE};
    
    // SPSC queues with proper atomics
    std::atomic<uint32_t> submit_head{0};
    std::atomic<uint32_t> submit_tail{0};
    std::atomic<uint32_t> free_head{0};
    std::atomic<uint32_t> free_tail{0};
    
    // Statistics
    std::atomic<uint64_t> events_written{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint32_t> ring_swaps{0};
    
    // Memory pointers - but now they point to STRUCTURED data
    LaneMemoryLayout* memory_layout{nullptr};
    
    // Initialize lane with structured memory
    void initialize(LaneMemoryLayout* layout, uint32_t num_rings, size_t ring_size, size_t event_size, uint32_t queue_capacity) {
        if (needs_log_thread_registry_registry) printf("DEBUG: Lane::initialize start - num_rings=%u, ring_size=%zu, qcap=%u\n", num_rings, ring_size, queue_capacity);
        memory_layout = layout;
        ring_count = num_rings;
        submit_capacity = queue_capacity;
        free_capacity = queue_capacity;
        
        // Ring buffers are already set up in memory, just verify pointers
        if (layout->ring_memory_base) {
            if (needs_log_thread_registry_registry) printf("DEBUG: Verifying %u ring buffer pointers\n", num_rings);
            for (uint32_t i = 0; i < num_rings; ++i) {
                if (!layout->ring_ptrs[i]) {
                    if (needs_log_thread_registry_registry) printf("DEBUG: ERROR - ring_ptrs[%u] is NULL!\n", i);
                } else {
                    if (needs_log_thread_registry_registry) printf("DEBUG: Ring[%u] at %p\n", i, layout->ring_ptrs[i]);
                }
            }
        } else {
            if (needs_log_thread_registry_registry) printf("DEBUG: ERROR - ring_memory_base is NULL!\n");
        }

        // Attach ring buffer handles for each ring
        for (uint32_t i = 0; i < num_rings; ++i) {
            if (layout->ring_ptrs[i]) {
                layout->rb_handles[i] = ring_buffer_attach(layout->ring_ptrs[i], ring_size, event_size);
            } else {
                layout->rb_handles[i] = nullptr;
            }
        }
        
        if (needs_log_thread_registry_registry) printf("DEBUG: Initializing free queue with num_rings=%u\n", num_rings);
        // Initialize free queue with all rings except active (0)
        for (uint32_t i = 1; i < num_rings; ++i) {
            if (needs_log_thread_registry_registry) { printf("DEBUG: Setting free_queue[%u] = %u\n", i-1, i); fflush(stdout); }
            layout->free_queue[i - 1] = i;
            if (needs_log_thread_registry_registry) { printf("DEBUG: free_queue[%u] set successfully\n", i-1); fflush(stdout); }
        }
        if (needs_log_thread_registry_registry) printf("DEBUG: Setting free_tail to %u\n", num_rings - 1);
        free_tail.store(num_rings - 1, std::memory_order_release);
        if (needs_log_thread_registry_registry) printf("DEBUG: Lane::initialize complete\n");
    }
    
    // Submit ring for draining (with bounds checking)
    bool submit_ring(uint32_t ring_idx) {
        if (ring_idx >= ring_count) return false;
        
        auto head = submit_head.load(std::memory_order_relaxed);
        auto tail = submit_tail.load(std::memory_order_acquire);
        auto next = (tail + 1) % submit_capacity;
        
        if (next == head) return false; // Queue full
        
        memory_layout->submit_queue[tail] = ring_idx;
        submit_tail.store(next, std::memory_order_release);
        return true;
    }
    
    // Get active ring buffer
    void* get_active_ring() {
        auto idx = active_idx.load(std::memory_order_relaxed);
        return memory_layout->rb_handles[idx];
    }
    
    // Debug print
    void debug_print() const {
        printf("Lane at %p:\n", this);
        printf("  active_idx: %u\n", active_idx.load());
        printf("  ring_count: %u\n", ring_count);
        printf("  events_written: %llu\n", (unsigned long long)events_written.load());
        if (memory_layout) {
            memory_layout->debug_print();
        }
    }
};

// ============================================================================
// ThreadLaneSet with explicit memory layout
// ============================================================================

class alignas(CACHE_LINE_SIZE) ThreadLaneSet {
public:
    // Thread identification
    uintptr_t thread_id{0};
    uint32_t slot_index{0};
    std::atomic<bool> active{false};
    
    // Lanes with structured memory
    Lane index_lane;
    Lane detail_lane;
    
    // Statistics
    std::atomic<uint64_t> events_generated{0};
    std::atomic<uint64_t> last_event_timestamp{0};
    
    // Initialize with structured memory blocks
    void initialize(uintptr_t tid, uint32_t slot,
                   LaneMemoryLayout* index_memory,
                   LaneMemoryLayout* detail_memory) {
        if (needs_log_thread_registry_registry) printf("DEBUG: ThreadLaneSet::initialize start - tid=%lx, slot=%u\n", tid, slot);
        thread_id = tid;
        slot_index = slot;
        
        if (needs_log_thread_registry_registry) printf("DEBUG: Initializing index_lane\n");
        index_lane.initialize(index_memory, RINGS_PER_INDEX_LANE, 64 * 1024, sizeof(IndexEvent), QUEUE_COUNT_INDEX_LANE);
        if (needs_log_thread_registry_registry) printf("DEBUG: Initializing detail_lane\n");
        detail_lane.initialize(detail_memory, RINGS_PER_DETAIL_LANE, 256 * 1024, sizeof(DetailEvent), QUEUE_COUNT_DETAIL_LANE);
        
        if (needs_log_thread_registry_registry) printf("DEBUG: Setting active flag\n");
        active.store(true, std::memory_order_release);
        if (needs_log_thread_registry_registry) printf("DEBUG: ThreadLaneSet::initialize complete\n");
    }
    
    // Debug helper
    void debug_print() const {
        printf("ThreadLaneSet[%u] (tid=%lx):\n", slot_index, (unsigned long)thread_id);
        printf("  active: %s\n", active.load() ? "yes" : "no");
        printf("  events_generated: %llu\n", (unsigned long long)events_generated.load());
        printf("  Index Lane:\n");
        index_lane.debug_print();
        printf("  Detail Lane:\n");
        detail_lane.debug_print();
    }
};

// ============================================================================
// ThreadRegistry with CRTP-style tail allocation
// ============================================================================

class ThreadRegistry {
public:
    // Header markers for attach validation
    uint32_t magic{0};
    uint32_t version{0};
    // Registry state
    std::atomic<uint32_t> thread_count{0};
    std::atomic<bool> accepting_registrations{true};
    std::atomic<bool> shutdown_requested{false};
    uint32_t capacity_{MAX_THREADS};
    // Multi-segment table (epoched)
    std::atomic<uint32_t> segment_count{0};
    std::atomic<uint32_t> epoch{0};
    // Keep small, enough for base + a few overflow pools
    SegmentInfo segments[8]{};
    
    // Thread lane sets - dynamically placed in shared memory
    ThreadLaneSet* thread_lanes{nullptr};
    
    // Factory method for creating with proper memory layout
    static ThreadRegistry* create(void* memory, size_t size, uint32_t capacity) {
        // Place lane set array after the header, cache-line aligned
        uint8_t* base_ptr = static_cast<uint8_t*>(memory);
        size_t lanes_off = (sizeof(ThreadRegistry) + (CACHE_LINE_SIZE - 1)) & ~(size_t)(CACHE_LINE_SIZE - 1);
        size_t lanes_bytes = (size_t)capacity * sizeof(ThreadLaneSet);
        // Ring pool starts after lane sets, page-align for rings
        size_t ring_off_unaligned = lanes_off + lanes_bytes;
        size_t ring_off = (ring_off_unaligned + 4095) & ~(size_t)4095;
        if (ring_off > size) {
            fprintf(stderr, "Insufficient memory for lane array: need at least %zu, got %zu\n", ring_off, size);
            return nullptr;
        }
        size_t ring_memory_total = size - ring_off;
        
        // Placement new with clear memory
        std::memset(memory, 0, size);
        auto* registry = new (memory) ThreadRegistry();
        // Initialize header markers
        registry->magic = 0x41544152; // 'ATAR' (ADA Thread ARchive marker)
        registry->version = 1;
        registry->capacity_ = capacity;
        registry->thread_lanes = reinterpret_cast<ThreadLaneSet*>(base_ptr + lanes_off);
        
        // Calculate where ring buffer memory starts (after all structures)
        uint8_t* ring_memory_start = base_ptr + ring_off;
        // Initialize segment table with one unified local pool
        registry->segment_count.store(1, std::memory_order_release);
        registry->epoch.store(1, std::memory_order_release);
        registry->segments[0].id = 1;
        registry->segments[0].kind = SEGMENT_KIND_OVERFLOW; // unified pool
        registry->segments[0].size = ring_memory_total;
        registry->segments[0].base_offset = (uint64_t)(ring_memory_start - static_cast<uint8_t*>(memory));
        std::snprintf(registry->segments[0].name, sizeof(registry->segments[0].name), "local:pool");
        
        // Initialize each thread slot (lane layouts will be allocated lazily at registration)
        for (uint32_t i = 0; i < capacity; ++i) {
            // Don't call initialize yet - just set slot index
            auto* lane_set = &registry->thread_lanes[i];
            // Zero-initialize the lane set storage
            std::memset(lane_set, 0, sizeof(ThreadLaneSet));
            lane_set->slot_index = i;
            lane_set->thread_id = 0;
            lane_set->active.store(false);
            // Lane layouts will be assigned on registration
            lane_set->index_lane.memory_layout = nullptr;
            lane_set->detail_lane.memory_layout = nullptr;
        }
        
        return registry;
    }
    
    // Register thread with better error handling
    ThreadLaneSet* register_thread(uintptr_t thread_id) {
        if (!accepting_registrations.load(std::memory_order_acquire)) {
            if (needs_log_thread_registry_registry) printf("DEBUG: Not accepting registrations\n");
            return nullptr;
        }
        
        // Try to find existing registration
        uint32_t current_count = thread_count.load();
        for (uint32_t i = 0; i < current_count; ++i) {
            if (thread_lanes[i].thread_id == thread_id && 
                thread_lanes[i].active.load()) {
                if (needs_log_thread_registry_registry) printf("DEBUG: Thread %lx already registered at slot %u\n", thread_id, i);
                return &thread_lanes[i];  // Already registered
            }
        }
        
        // Allocate new slot
        uint32_t slot = thread_count.fetch_add(1, std::memory_order_acq_rel);
        if (needs_log_thread_registry_registry) printf("DEBUG: Allocating slot %u for thread %lx (capacity=%u)\n", slot, thread_id, capacity_);
        if (slot >= capacity_) {
            thread_count.fetch_sub(1, std::memory_order_acq_rel);
            if (needs_log_thread_registry_registry) printf("DEBUG: Out of slots! slot=%u >= capacity=%u\n", slot, capacity_);
            return nullptr;
        }
        
        // Initialize slot (only if not already initialized)
        if (thread_lanes[slot].thread_id == 0) {
            // Resolve segment bases
            uint8_t* base = reinterpret_cast<uint8_t*>(this);
            uint8_t* pool_base = base + segments[0].base_offset; // unified pool id=1
            // Helper lambda: allocate from segment with alignment
            auto alloc_from = [](std::atomic<uint64_t>& used, uint64_t size, uint64_t seg_size, uint32_t align) -> uint64_t {
                uint64_t cur = used.load(std::memory_order_relaxed);
                for (;;) {
                    uint64_t aligned = (cur + (align - 1)) & ~(uint64_t)(align - 1);
                    uint64_t next = aligned + size;
                    if (next > seg_size) return UINT64_MAX;
                    if (used.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                        return aligned;
                    }
                    // CAS failed; cur updated, retry
                }
            };
            // Allocate lane layouts from unified pool
            uint64_t idx_layout_off = alloc_from(segments[0].used, sizeof(LaneMemoryLayout), segments[0].size, CACHE_LINE_SIZE);
            if (idx_layout_off == UINT64_MAX) {
                thread_count.fetch_sub(1, std::memory_order_acq_rel);
                if (needs_log_thread_registry_registry) printf("DEBUG: Out of metadata memory while registering thread %lx (index layout)\n", thread_id);
                return nullptr;
            }
            auto* idx_layout = reinterpret_cast<LaneMemoryLayout*>(pool_base + idx_layout_off);
            std::memset(idx_layout, 0, sizeof(LaneMemoryLayout));
            idx_layout->ring_memory_base = pool_base;
            idx_layout->ring_memory_size = segments[0].size;

            uint64_t det_layout_off = alloc_from(segments[0].used, sizeof(LaneMemoryLayout), segments[0].size, CACHE_LINE_SIZE);
            if (det_layout_off == UINT64_MAX) {
                thread_count.fetch_sub(1, std::memory_order_acq_rel);
                if (needs_log_thread_registry_registry) printf("DEBUG: Out of metadata memory while registering thread %lx (detail layout)\n", thread_id);
                return nullptr;
            }
            auto* det_layout = reinterpret_cast<LaneMemoryLayout*>(pool_base + det_layout_off);
            std::memset(det_layout, 0, sizeof(LaneMemoryLayout));
            det_layout->ring_memory_base = pool_base;
            det_layout->ring_memory_size = segments[0].size;
            // Allocate index rings (from unified pool segment[0])
            for (uint32_t j = 0; j < RINGS_PER_INDEX_LANE; ++j) {
                uint64_t off = alloc_from(segments[0].used, 64 * 1024, segments[0].size, 4096);
                if (off == UINT64_MAX) {
                    // Out of ring memory
                    thread_count.fetch_sub(1, std::memory_order_acq_rel);
                    if (needs_log_thread_registry_registry) printf("DEBUG: Out of index ring memory while registering thread %lx\n", thread_id);
                    return nullptr;
                }
                uint8_t* ring_ptr = pool_base + off;
                idx_layout->ring_ptrs[j] = ring_ptr;
                idx_layout->ring_descs[j].segment_id = 1;
                idx_layout->ring_descs[j].bytes = 64 * 1024;
                idx_layout->ring_descs[j].offset = off;
            }
            // Allocate detail rings (from unified pool segment[0])
            for (uint32_t j = 0; j < RINGS_PER_DETAIL_LANE; ++j) {
                uint64_t off = alloc_from(segments[0].used, 256 * 1024, segments[0].size, 4096);
                if (off == UINT64_MAX) {
                    thread_count.fetch_sub(1, std::memory_order_acq_rel);
                    if (needs_log_thread_registry_registry) printf("DEBUG: Out of detail ring memory while registering thread %lx\n", thread_id);
                    return nullptr;
                }
                uint8_t* ring_ptr = pool_base + off;
                det_layout->ring_ptrs[j] = ring_ptr;
                det_layout->ring_descs[j].segment_id = 1;
                det_layout->ring_descs[j].bytes = 256 * 1024;
                det_layout->ring_descs[j].offset = off;
            }
            
            thread_lanes[slot].initialize(
                thread_id, 
                slot,
                idx_layout,
                det_layout
            );
        } else {
            thread_lanes[slot].thread_id = thread_id;
            thread_lanes[slot].active.store(true, std::memory_order_release);
        }
        
        if (needs_log_thread_registry_registry) { printf("DEBUG: Returning thread_lanes[%u] at %p\n", slot, &thread_lanes[slot]); fflush(stdout); }
        return &thread_lanes[slot];
    }
    
    // Debug dump - actually useful!
    void debug_dump() const {
        printf("=== ThreadRegistry Debug Dump ===\n");
        printf("Address: %p\n", this);
        printf("Thread count: %u\n", thread_count.load());
        printf("Accepting: %s\n", accepting_registrations.load() ? "yes" : "no");
        printf("Shutdown: %s\n", shutdown_requested.load() ? "yes" : "no");
        printf("Segments (epoch=%u, count=%u):\n", epoch.load(), segment_count.load());
        for (uint32_t i = 0; i < segment_count.load(); ++i) {
            const auto& s = segments[i];
            printf("  seg[%u]: id=%u kind=%u size=%llu base_off=0x%llx name=%s\n",
                   i, s.id, s.kind,
                   (unsigned long long)s.size,
                   (unsigned long long)s.base_offset,
                   s.name);
        }
        
        for (uint32_t i = 0; i < thread_count.load(); ++i) {
            if (thread_lanes[i].active.load()) {
                thread_lanes[i].debug_print();
            }
        }
        printf("=================================\n");
    }
    
    // Validation helper
    bool validate() const {
        // Check alignment
        if (reinterpret_cast<uintptr_t>(this) % CACHE_LINE_SIZE != 0) {
            fprintf(stderr, "Registry not cache-aligned\n");
            return false;
        }
        // Check markers
        if (magic != 0x41544152 || version != 1) {
            fprintf(stderr, "Registry magic/version invalid (magic=0x%x version=%u)\n", magic, version);
            return false;
        }
        
        // Check thread lanes alignment
        for (uint32_t i = 0; i < capacity_; ++i) {
            auto addr = reinterpret_cast<uintptr_t>(&thread_lanes[i]);
            if (addr % CACHE_LINE_SIZE != 0) {
                fprintf(stderr, "ThreadLane[%u] not cache-aligned\n", i);
                return false;
            }
        }
        
        return true;
    }
    
    // Accessors
    uint32_t get_capacity() const;
};

// Public accessor for capacity from C layer
inline uint32_t ThreadRegistry::get_capacity() const { return capacity_; }

// ============================================================================
// C compatibility layer
// ============================================================================

// Note: C compatibility layer is implemented in thread_registry.cpp
// to avoid duplicate symbols and provide proper separation

// ============================================================================
// Conversion Functions for Tests
// ============================================================================
// These inline functions provide safe conversion from opaque C pointers to
// C++ types for testing purposes. They should only be used in test code.

inline ThreadLaneSet* to_cpp(::ThreadLaneSet* c_ptr) {
    return reinterpret_cast<ThreadLaneSet*>(c_ptr);
}

inline const ThreadLaneSet* to_cpp(const ::ThreadLaneSet* c_ptr) {
    return reinterpret_cast<const ThreadLaneSet*>(c_ptr);
}

inline ThreadRegistry* to_cpp(::ThreadRegistry* c_ptr) {
    return reinterpret_cast<ThreadRegistry*>(c_ptr);
}

inline const ThreadRegistry* to_cpp(const ::ThreadRegistry* c_ptr) {
    return reinterpret_cast<const ThreadRegistry*>(c_ptr);
}

inline Lane* to_cpp(::Lane* c_ptr) {
    return reinterpret_cast<Lane*>(c_ptr);
}

inline const Lane* to_cpp(const ::Lane* c_ptr) {
    return reinterpret_cast<const Lane*>(c_ptr);
}

} // namespace internal
} // namespace ada

#endif // THREAD_REGISTRY_PRIVATE_H
