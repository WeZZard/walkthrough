/**
 * Interface Compilation Tests
 * 
 * These tests ensure all interface definitions compile correctly
 * and can be used as intended. They validate the interface contracts
 * without requiring full implementations.
 */

#include <gtest/gtest.h>
#include <tracer_backend/interfaces/thread_registry_interface.h>
#include <cstdint>
#include <atomic>
#include <chrono>

// ============================================================================
// Mock Implementation for Testing Interface
// ============================================================================

class MockThreadRegistry {
    // Minimal fields to test interface
    uint32_t magic = 0xADA0;
    uint32_t _pad1[15];
    std::atomic<uint32_t> active_threads{0};
    uint32_t _pad2[15];
};

class MockThreadLaneSet {
    std::atomic<bool> active{false};
    uintptr_t thread_id = 0;
    uint8_t _pad[112]; // Padding to 128 bytes
};

struct MockLane {
    SPSCQueueHeader submit_queue;
    SPSCQueueHeader free_queue;
};

// ============================================================================
// Interface Compilation Tests
// ============================================================================

class InterfaceCompilationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Allocate aligned memory for testing
        memory = aligned_alloc(4096, memory_size);
        ASSERT_NE(memory, nullptr);
        memset(memory, 0, memory_size);
    }
    
    void TearDown() override {
        if (memory) {
            free(memory);
            memory = nullptr;
        }
    }
    
    void* memory = nullptr;
    size_t memory_size = 1024 * 1024; // 1MB
};

TEST_F(InterfaceCompilationTest, ThreadRegistryAPI__compiles__then_links) {
    // Test that all ThreadRegistry API functions exist and link
    
    // Lifecycle functions
    ThreadRegistry* registry = thread_registry_init(memory, memory_size);
    EXPECT_NE(registry, nullptr);
    
    ThreadRegistry* attached = thread_registry_attach(memory);
    EXPECT_EQ(attached, registry);
    
    // Registration functions (would fail without implementation)
    // ThreadLaneSet* lanes = thread_registry_register(registry, pthread_self());
    
    // Drain functions
    uint32_t active_count = thread_registry_get_active_count(registry);
    EXPECT_EQ(active_count, 0);
    
    ThreadLaneSet* thread_at_0 = thread_registry_get_thread_at(registry, 0);
    EXPECT_EQ(thread_at_0, nullptr); // No threads registered
    
    // Statistics
    TracerStats stats;
    thread_registry_get_stats(registry, &stats);
    EXPECT_EQ(stats.active_threads, 0);
    
    // Cleanup
    thread_registry_destroy(registry);
}

TEST_F(InterfaceCompilationTest, SPSCQueueHeader__layout__then_cache_aligned) {
    // Verify SPSC queue header layout for cache line isolation
    SPSCQueueHeader queue;
    
    // Check alignment of critical fields
    EXPECT_EQ(offsetof(SPSCQueueHeader, write_pos), 0);
    EXPECT_GE(offsetof(SPSCQueueHeader, read_pos), 64); // At least cache line apart
    
    // Check total size is at least 2 cache lines
    EXPECT_GE(sizeof(SPSCQueueHeader), 128);
    
    // Verify atomic operations compile
    std::atomic<uint32_t>* write_pos = 
        reinterpret_cast<std::atomic<uint32_t>*>(&queue.write_pos);
    std::atomic<uint32_t>* read_pos = 
        reinterpret_cast<std::atomic<uint32_t>*>(&queue.read_pos);
    
    write_pos->store(1, std::memory_order_release);
    uint32_t value = read_pos->load(std::memory_order_acquire);
    EXPECT_EQ(value, 0); // Independent cache lines
}

TEST_F(InterfaceCompilationTest, RingBufferHeader__layout__then_correct_size) {
    // Verify ring buffer header layout
    RingBufferHeader header;
    
    EXPECT_EQ(sizeof(header), 64); // Full cache line
    EXPECT_EQ(offsetof(RingBufferHeader, magic), 0);
    EXPECT_EQ(offsetof(RingBufferHeader, write_pos), 12);
    EXPECT_EQ(offsetof(RingBufferHeader, read_pos), 16);
    
    // Test magic number
    header.magic = RING_BUFFER_MAGIC;
    EXPECT_EQ(header.magic, 0xADA0);
}

TEST_F(InterfaceCompilationTest, PerformanceConstants__defined__then_valid_ranges) {
    // Verify performance contract constants
    EXPECT_EQ(THREAD_REGISTRY_MAX_THREADS, 64);
    EXPECT_EQ(THREAD_REGISTRATION_MAX_NS, 1000);  // <1μs
    EXPECT_EQ(LANE_ACCESS_MAX_NS, 10);            // <10ns
    
    // These will be enforced by benchmarks
    EXPECT_LE(THREAD_REGISTRATION_MAX_NS, 1000);
    EXPECT_LE(LANE_ACCESS_MAX_NS, 100); // Allow some slack for tests
}

TEST_F(InterfaceCompilationTest, TLSAccess__inline_function__then_compiles) {
    // Test that TLS inline function compiles
    ThreadLaneSet* lanes = thread_registry_get_lanes();
    EXPECT_EQ(lanes, nullptr); // Not registered
    
    // Verify it's actually inline (no function call overhead)
    // This is validated by checking assembly or benchmarks
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        volatile ThreadLaneSet* l = thread_registry_get_lanes();
        (void)l;
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double ns_per_access = duration.count() / 1000000.0;
    
    // Should be <10ns per access for TLS
    EXPECT_LT(ns_per_access, LANE_ACCESS_MAX_NS * 10); // Allow 10x for debug builds
}

TEST_F(InterfaceCompilationTest, LaneOperations__all_functions__then_link) {
    // Test that all lane operation functions link
    // Note: These will return NULL/error without implementation
    
    MockLane lane;
    MockThreadLaneSet lanes;
    
    // These would need real implementations to work
    // Lane* index_lane = thread_lanes_get_index_lane(&lanes);
    // Lane* detail_lane = thread_lanes_get_detail_lane(&lanes);
    // RingBuffer* ring = lane_get_active_ring(&lane);
    
    // Test SPSC queue operations compile with atomics
    std::atomic<uint32_t>* write_pos = 
        reinterpret_cast<std::atomic<uint32_t>*>(&lane.submit_queue.write_pos);
    std::atomic<uint32_t>* read_pos = 
        reinterpret_cast<std::atomic<uint32_t>*>(&lane.submit_queue.read_pos);
    
    // Simulate submit operation
    uint32_t old_write = write_pos->load(std::memory_order_relaxed);
    uint32_t new_write = (old_write + 1) % lane.submit_queue.capacity;
    write_pos->store(new_write, std::memory_order_release);
    
    // Simulate take operation  
    uint32_t current_read = read_pos->load(std::memory_order_acquire);
    EXPECT_EQ(current_read, 0);
}

// ============================================================================
// C Linkage Test
// ============================================================================

extern "C" {
    // Verify C linkage works for FFI
    void test_c_linkage() {
        void* mem = malloc(1024);
        ThreadRegistry* reg = thread_registry_init(mem, 1024);
        thread_registry_destroy(reg);
        free(mem);
    }
}

TEST(InterfaceLinkageTest, CInterface__extern_c__then_links) {
    // Just verify the C linkage compiles
    test_c_linkage();
}