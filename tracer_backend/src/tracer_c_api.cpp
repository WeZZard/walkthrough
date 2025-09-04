/**
 * C API Implementation for Rust FFI
 * 
 * This file provides the C interface defined in tracer_backend.h
 * It wraps the internal C++ implementation for use by Rust.
 */

// Include internal headers first (they have full definitions)
#include "controller/frida_controller_internal.h"
#include "utils/ring_buffer_private.h"
#include "utils/thread_registry_private.h"

// Then include the umbrella header for the C API declarations
extern "C" {
#include <tracer_backend/tracer_backend.h>
}
#include <memory>
#include <vector>
#include <cstring>

// ============================================================================
// Internal Implementation
// ============================================================================

namespace {

struct TracerImpl {
    std::unique_ptr<ada::internal::FridaController> controller;
    ada::internal::ThreadRegistry* registry = nullptr;
    std::vector<ada::internal::RingBuffer*> ring_buffers;
    
    TracerImpl(const char* output_dir) 
        : controller(std::make_unique<ada::internal::FridaController>(output_dir)) {
    }
};

struct DrainImpl {
    TracerImpl* tracer;
    uint32_t current_thread = 0;
    
    explicit DrainImpl(TracerImpl* t) : tracer(t) {}
};

} // anonymous namespace

// ============================================================================
// Tracer Control API Implementation
// ============================================================================

extern "C" {

TracerHandle* tracer_create(const char* output_dir) {
    try {
        return reinterpret_cast<TracerHandle*>(new TracerImpl(output_dir));
    } catch (...) {
        return nullptr;
    }
}

void tracer_destroy(TracerHandle* tracer) {
    delete reinterpret_cast<TracerImpl*>(tracer);
}

int tracer_spawn(TracerHandle* tracer, const char* path, 
                 char* const argv[], uint32_t* out_pid) {
    if (!tracer) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    return impl->controller->spawn_suspended(path, argv, out_pid);
}

int tracer_attach(TracerHandle* tracer, uint32_t pid) {
    if (!tracer) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    return impl->controller->attach(pid);
}

int tracer_detach(TracerHandle* tracer) {
    if (!tracer) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    return impl->controller->detach();
}

int tracer_resume(TracerHandle* tracer) {
    if (!tracer) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    return impl->controller->resume();
}

int tracer_install_hooks(TracerHandle* tracer) {
    if (!tracer) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    return impl->controller->install_hooks();
}

// ============================================================================
// Event Draining API Implementation
// ============================================================================

DrainHandle* tracer_create_drain(TracerHandle* tracer) {
    if (!tracer) return nullptr;
    
    try {
        auto* impl = reinterpret_cast<TracerImpl*>(tracer);
        return reinterpret_cast<DrainHandle*>(new DrainImpl(impl));
    } catch (...) {
        return nullptr;
    }
}

size_t tracer_drain_events(DrainHandle* drain, uint8_t* buffer, size_t buffer_size) {
    if (!drain || !buffer || buffer_size == 0) return 0;
    
    auto* impl = reinterpret_cast<DrainImpl*>(drain);
    
    // This is a simplified implementation
    // In reality, would iterate through thread registry and drain ring buffers
    
    // For now, return 0 (no events)
    // TODO: Implement actual draining logic once thread registry is accessible
    return 0;
}

size_t tracer_serialize_events(const uint8_t* buffer, size_t size,
                               uint8_t* output, size_t output_size) {
    if (!buffer || !output || size == 0 || output_size == 0) return 0;
    
    // For now, just copy raw bytes
    // TODO: Implement actual serialization when event format is stabilized
    size_t copy_size = (size < output_size) ? size : output_size;
    std::memcpy(output, buffer, copy_size);
    return copy_size;
}

void tracer_destroy_drain(DrainHandle* drain) {
    delete reinterpret_cast<DrainImpl*>(drain);
}

// ============================================================================
// Shared Memory Access API Implementation
// ============================================================================

RingBufferHeader* tracer_get_ring_buffer_header(TracerHandle* tracer, int lane_type) {
    if (!tracer) return nullptr;
    
    // TODO: Implement once we have access to shared memory through controller
    // For now, return nullptr
    return nullptr;
}

size_t tracer_get_ring_buffer_size(TracerHandle* tracer, int lane_type) {
    if (!tracer) return 0;
    
    // TODO: Implement based on lane type
    const size_t INDEX_LANE_SIZE = 32 * 1024 * 1024;  // 32MB
    const size_t DETAIL_LANE_SIZE = 32 * 1024 * 1024; // 32MB
    
    return (lane_type == 0) ? INDEX_LANE_SIZE : DETAIL_LANE_SIZE;
}

// ============================================================================
// Statistics API Implementation
// ============================================================================

int tracer_get_stats(TracerHandle* tracer, TracerStats* stats) {
    if (!tracer || !stats) return -1;
    
    auto* impl = reinterpret_cast<TracerImpl*>(tracer);
    
    // Get stats from controller
    auto controller_stats = impl->controller->get_stats();
    
    // Map to C API stats
    stats->events_captured = controller_stats.events_captured;
    stats->events_dropped = controller_stats.events_dropped;
    stats->bytes_written = controller_stats.bytes_written;
    stats->active_threads = controller_stats.active_threads;
    stats->hooks_installed = controller_stats.hooks_installed;
    
    return 0;
}

} // extern "C"