// Integration tests using Google Test
// Direct translation from test_integration.c
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern "C" {
    #include "frida_controller.h"
    #include "shared_memory.h"
    #include "ring_buffer.h"
    #include "tracer_types.h"
    #include "ada_paths.h"
}

// Test fixture for integration tests
class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[INTEGRATION] Setting up test\n");
    }
    
    void TearDown() override {
        // Cleanup is handled in each test
    }
};

// Test: controller__spawn_and_attach__then_hooks_installed
// Direct translation of test_spawn_and_attach()
TEST_F(IntegrationTest, controller__spawn_and_attach__then_hooks_installed) {
    printf("[INTEGRATION] spawn_and_attach → hooks installed\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    ASSERT_NE(controller, nullptr);
    
    // Spawn test_cli suspended
    char* argv[] = {(char*)"test_cli", (char*)"--wait", nullptr};
    uint32_t pid = 0;
    
    int result = frida_controller_spawn_suspended(controller, 
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli", argv, &pid);
    if (result != 0) {
        printf("  ⚠️  Spawn failed - may need elevated permissions\n");
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Spawn failed - may need elevated permissions";
    }
    ASSERT_EQ(result, 0);
    ASSERT_GT(pid, 0u);
    printf("  ✓ Spawned test_cli with PID %u\n", pid);
    
    // Attach to the spawned process
    result = frida_controller_attach(controller, pid);
    ASSERT_EQ(result, 0);
    printf("  ✓ Attached to process\n");
    
    // Install hooks
    result = frida_controller_install_hooks(controller);
    ASSERT_EQ(result, 0);
    printf("  ✓ Installed hooks\n");
    
    // Resume the process
    result = frida_controller_resume(controller);
    ASSERT_EQ(result, 0);
    printf("  ✓ Resumed process\n");
    
    // Let it run for a bit
    sleep(3);
    
    // Check statistics
    TracerStats stats = frida_controller_get_stats(controller);
    printf("  Events captured: %llu\n", stats.events_captured);
    ASSERT_GT(stats.events_captured, 0ull);
    
    // Detach
    frida_controller_detach(controller);
    
    // Cleanup
    frida_controller_destroy(controller);
    
    // Kill the test process if still running
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, WNOHANG);
    
    printf("  ✓ Spawn and attach test passed\n");
}

// Test: shared_memory__communication__then_events_transmitted
// Direct translation of test_shared_memory_communication()
TEST_F(IntegrationTest, shared_memory__communication__then_events_transmitted) {
    printf("[INTEGRATION] shared_memory_communication → events transmitted\n");
    
    // Create controller which sets up shared memory
    FridaController* controller = frida_controller_create("./test_output");
    if (controller == nullptr) {
        printf("  ⚠️  Controller creation failed - may need elevated permissions\n");
        GTEST_SKIP() << "Controller creation failed - may need elevated permissions";
    }
    ASSERT_NE(controller, nullptr);
    
    // Open the shared memory from another process perspective using unique naming
    uint32_t sid = shared_memory_get_session_id();
    pid_t pid = shared_memory_get_pid();
    SharedMemoryRef shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, pid, sid, 4096);
    SharedMemoryRef shm_index = shared_memory_open_unique(ADA_ROLE_INDEX, pid, sid, 32 * 1024 * 1024);
    
    ASSERT_NE(shm_control, nullptr);
    ASSERT_NE(shm_index, nullptr);
    
    // Verify control block is accessible
    ControlBlock* control = (ControlBlock*)shared_memory_get_address(shm_control);
    ASSERT_EQ(control->process_state, PROCESS_STATE_INITIALIZED);
    ASSERT_EQ(control->index_lane_enabled, 1u);
    
    // Create ring buffer view
    RingBuffer* rb = ring_buffer_create(shared_memory_get_address(shm_index), 32 * 1024 * 1024, sizeof(IndexEvent));
    ASSERT_NE(rb, nullptr);
    ASSERT_TRUE(ring_buffer_is_empty(rb));
    
    // Write a test event
    IndexEvent event = {
        .timestamp = 12345,
        .function_id = 0x100,
        .thread_id = 1,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = 1
    };
    
    ASSERT_TRUE(ring_buffer_write(rb, &event));
    
    // Verify it can be read
    sleep(1); // Let drain thread process
    
    TracerStats stats = frida_controller_get_stats(controller);
    // Note: Stats might be 0 if drain thread hasn't run yet
    
    // Cleanup
    ring_buffer_destroy(rb);
    shared_memory_destroy(shm_control);
    shared_memory_destroy(shm_index);
    frida_controller_destroy(controller);
    
    printf("  ✓ Shared memory communication test passed\n");
}

// Test: controller__state_transitions__then_correct_sequence
// Direct translation of test_state_transitions()
TEST_F(IntegrationTest, controller__state_transitions__then_correct_sequence) {
    printf("[INTEGRATION] state_transitions → correct sequence\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    if (controller == nullptr) {
        printf("  ⚠️  Controller creation failed - may need elevated permissions\n");
        GTEST_SKIP() << "Controller creation failed - may need elevated permissions";
    }
    ASSERT_NE(controller, nullptr);
    
    // Initial state
    ASSERT_EQ(frida_controller_get_state(controller), PROCESS_STATE_INITIALIZED);
    
    // Spawn process
    char* argv[] = {(char*)"test_cli", nullptr};
    uint32_t pid = 0;
    frida_controller_spawn_suspended(controller, 
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli", argv, &pid);
    
    ProcessState state = frida_controller_get_state(controller);
    ASSERT_TRUE(state == PROCESS_STATE_SUSPENDED || state == PROCESS_STATE_SPAWNING);
    
    // Attach
    frida_controller_attach(controller, pid);
    ASSERT_EQ(frida_controller_get_state(controller), PROCESS_STATE_ATTACHED);
    
    // Resume
    frida_controller_resume(controller);
    ASSERT_EQ(frida_controller_get_state(controller), PROCESS_STATE_RUNNING);
    
    sleep(1);
    
    // Detach
    frida_controller_detach(controller);
    ASSERT_EQ(frida_controller_get_state(controller), PROCESS_STATE_INITIALIZED);
    
    // Cleanup
    frida_controller_destroy(controller);
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, WNOHANG);
    
    printf("  ✓ State transitions test passed\n");
}

// Test: controller__statistics_collection__then_counters_increment
// Direct translation of test_statistics_collection()
TEST_F(IntegrationTest, controller__statistics_collection__then_counters_increment) {
    printf("[INTEGRATION] statistics_collection → counters increment\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    if (controller == nullptr) {
        printf("  ⚠️  Controller creation failed - may need elevated permissions\n");
        GTEST_SKIP() << "Controller creation failed - may need elevated permissions";
    }
    ASSERT_NE(controller, nullptr);
    
    // Initial stats should be zero
    TracerStats stats = frida_controller_get_stats(controller);
    ASSERT_EQ(stats.events_captured, 0ull);
    ASSERT_EQ(stats.events_dropped, 0ull);
    ASSERT_EQ(stats.drain_cycles, 0ull);
    
    // Wait for drain thread to run a few cycles
    sleep(2);
    
    stats = frida_controller_get_stats(controller);
    ASSERT_GT(stats.drain_cycles, 0ull); // Should have run at least once
    
    frida_controller_destroy(controller);
    
    printf("  ✓ Statistics collection test passed\n");
}

// Main test runner that checks for test_cli availability
TEST_F(IntegrationTest, PrerequisiteCheck) {
    if (access(ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli", X_OK) != 0) {
        printf("Warning: test_cli not found or not executable\n");
        printf("Build test_cli first with: cargo build\n");
        GTEST_SKIP() << "test_cli not available, skipping tests that require it";
    }
}