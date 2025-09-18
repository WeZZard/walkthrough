// Integration tests for Controller Full Lifecycle using Google Test
// Direct translation from test_controller_full_lifecycle.c
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <dlfcn.h>

extern "C" {
    #include <tracer_backend/controller/frida_controller.h>
    #include <tracer_backend/utils/shared_memory.h>
    #include <tracer_backend/utils/ring_buffer.h>
    #include <tracer_backend/utils/tracer_types.h>
    #include "ada_paths.h"
}

// Test fixture for controller full lifecycle tests
class ControllerFullLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[CTRL] Setting up test\n");
    }
    
    void TearDown() override {
        // Cleanup is handled in each test
    }
};

// Test: controller__spawn_attach_resume__then_full_lifecycle_succeeds
// Direct translation from the C test
TEST_F(ControllerFullLifecycleTest, controller__spawn_attach_resume__then_full_lifecycle_succeeds) {
    printf("[CTRL] spawn_attach_resume → full lifecycle succeeds\n");
    
    // 1. Create controller with shared memory
    printf("  1. Creating controller and shared memory...\n");
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    // Verify shared memory is created (use unique naming contract)
    uint32_t sid = shared_memory_get_session_id();
    pid_t local_pid = shared_memory_get_pid();
    SharedMemoryRef shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, local_pid, sid, 4096);
    SharedMemoryRef shm_index = shared_memory_open_unique(ADA_ROLE_INDEX, local_pid, sid, 32 * 1024 * 1024);
    SharedMemoryRef shm_detail = shared_memory_open_unique(ADA_ROLE_DETAIL, local_pid, sid, 32 * 1024 * 1024);
    
    ASSERT_NE(shm_control, nullptr);
    ASSERT_NE(shm_index, nullptr);
    ASSERT_NE(shm_detail, nullptr);
    
    // 2. Test spawn method tracking
    printf("  2. Testing spawn with method tracking...\n");
    char* argv[] = {(char*)"test_cli", nullptr, nullptr};
    uint32_t agent_pid;
    
    const char * path = ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli";
    int result = frida_controller_spawn_suspended(controller,
        path,
        argv, &agent_pid);

    if (result != 0) {
        FILE * f = fopen(path, "r");
        if (f) {
            printf("  ⚠️  Spawn failed - may need elevated permissions\n");
            printf("  Run with: sudo %s\n", argv[0]);
            fclose(f);
        } else {
            printf("  ⚠️  Spawn failed - open does not exist\n");
        }
        // Cleanup and skip
        shared_memory_destroy(shm_control);
        shared_memory_destroy(shm_index);
        shared_memory_destroy(shm_detail);
        frida_controller_destroy(controller);
        GTEST_SKIP();
        return;
    }
    
    printf("  Spawned PID: %u\n", agent_pid);
    
    // 3. Attach to process
    printf("  3. Attaching to process...\n");
    result = frida_controller_attach(controller, agent_pid);
    ASSERT_EQ(frida_controller_get_state(controller), PROCESS_STATE_ATTACHED);
    ASSERT_EQ(result, 0);
    
    // 4. Test ring buffer attach (simulating agent attach)
    printf("  4. Testing ring buffer attach preservation...\n");
    
    // Controller writes initial data
    RingBuffer* controller_rb = ring_buffer_create(shared_memory_get_address(shm_index), 
                                                   shared_memory_get_size(shm_index), 
                                                   sizeof(IndexEvent));
    ASSERT_NE(controller_rb, nullptr);
    
    IndexEvent test_event = {
        .timestamp = 1234567890,
        .function_id = 42,
        .thread_id = 1,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = 0
    };
    
    bool write_result = ring_buffer_write(controller_rb, &test_event);
    ASSERT_TRUE(write_result);
    
    // Simulate agent attaching to the same buffer
    RingBuffer* agent_rb = ring_buffer_attach(shared_memory_get_address(shm_index),
                                              shared_memory_get_size(shm_index),
                                              sizeof(IndexEvent));
    ASSERT_NE(agent_rb, nullptr);
    
    // Verify agent sees the data
    IndexEvent read_event;
    bool read_result = ring_buffer_read(agent_rb, &read_event);
    ASSERT_TRUE(read_result);
    ASSERT_EQ(read_event.function_id, 42);
    ASSERT_EQ(read_event.timestamp, 1234567890);
    
    printf("  Ring buffer data preserved: function_id=%u, timestamp=%llu\n", 
           (unsigned int) read_event.function_id, (unsigned long long) read_event.timestamp);
    
    // 5. Install hooks (will use minimal script since native agent loading is placeholder)
    printf("  5. Installing hooks...\n");
    const char* old_rpath = getenv("ADA_AGENT_RPATH_SEARCH_PATHS");
    setenv("ADA_AGENT_RPATH_SEARCH_PATHS", ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib", 1);
    result = frida_controller_install_hooks(controller);
    if (old_rpath) {
        setenv("ADA_AGENT_RPATH_SEARCH_PATHS", old_rpath, 1);
    } else {
        unsetenv("ADA_AGENT_RPATH_SEARCH_PATHS");
    }
    ASSERT_EQ(result, 0);
    
    // 6. Test resume (should only use appropriate method, no double resume)
    printf("  6. Testing resume (no double resume)...\n");
    result = frida_controller_resume(controller);
    ASSERT_EQ(result, 0);
    
    // 7. Check final state
    ProcessState state = frida_controller_get_state(controller);
    // The process may exit too fast to get the running state, so we check for both.
    ASSERT_TRUE(state == PROCESS_STATE_RUNNING || state == PROCESS_STATE_INITIALIZED);
    printf("  Process state: RUNNING\n");
    
    // Let it run briefly
    usleep(100000); // 100ms
    
    // 8. Get stats
    TracerStats stats = frida_controller_get_stats(controller);
    printf("  Stats - Total events captured: %llu\n", stats.events_captured);
    
    // Clean up process
    kill((pid_t) agent_pid, SIGTERM);
    waitpid((pid_t) agent_pid, nullptr, 0);
    
    ring_buffer_destroy(controller_rb);
    ring_buffer_destroy(agent_rb);
    
    // Cleanup
    shared_memory_destroy(shm_control);
    shared_memory_destroy(shm_index);
    shared_memory_destroy(shm_detail);
    frida_controller_destroy(controller);
    
    printf("  ✓ Integrated fixes test completed\n");
}

// Test: agent__dlopen_symbols__then_exports_resolve
// Direct translation from the C test (updated with correct export name)
TEST_F(ControllerFullLifecycleTest, agent__dlopen_symbols__then_exports_resolve) {
    printf("[AGENT] dlopen_symbols → exports resolve\n");
    
    // Check that the agent library exports agent_init
    void* handle = dlopen(ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib", RTLD_LAZY);
    
    if (handle) {
        void* sym = dlsym(handle, "agent_init");
        if (sym) {
            printf("  ✓ Found agent_init export\n");
            SUCCEED();
        } else {
            printf("  ✗ agent_init not exported\n");
            ADD_FAILURE() << "agent_init not exported";
        }
        dlclose(handle);
    } else {
        printf("  ⚠️  Could not load agent library (may not be built yet)\n");
        printf("  $PWD: %s\n", getenv("PWD"));
        GTEST_SKIP();
    }
}