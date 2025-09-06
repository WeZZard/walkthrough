// Integration test for M1_E1_I1 Agent Loader using Google Test
// Direct translation from test_agent_loader.c
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern "C" {
    #include <tracer_backend/controller/frida_controller.h>
    #include <tracer_backend/utils/shared_memory.h>
    #include <tracer_backend/utils/tracer_types.h>
    #include "ada_paths.h"
}

// Test fixture for agent loader tests
class AgentLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[LOADER] Setting up test\n");
    }
    
    void TearDown() override {
        // Cleanup is handled in each test
    }
};

// Test: agent_loader__load_and_init__then_hooks_installed
// Direct translation from the C test
TEST_F(AgentLoaderTest, agent_loader__load_and_init__then_hooks_installed) {
    printf("[LOADER] load_and_init → hooks installed\n");
    
    // 1. Create controller
    printf("  1. Creating controller...\n");
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    // 2. Spawn test program
    printf("  2. Spawning test_cli...\n");
    char* argv[] = {(char*)"test_cli", (char*)"--brief", nullptr};
    uint32_t agent_pid;
    
    int result = frida_controller_spawn_suspended(controller,
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli",
        argv, &agent_pid);
    
    if (result != 0) {
        printf("  ⚠️  Spawn failed - may need elevated permissions\n");
        printf("  Run with: sudo <test_binary>\n");
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Spawn failed - may need elevated permissions";
        return;
    }
    
    printf("  Spawned PID: %u\n", agent_pid);
    
    // 3. Attach to process
    printf("  3. Attaching to process...\n");
    result = frida_controller_attach(controller, agent_pid);
    ASSERT_EQ(result, 0);
    
    // 4. Inject agent explicitly (align with BaselineHooks path)
    printf("  4. Injecting agent...\n");
    result = frida_controller_inject_agent(controller,
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib");

    if (result != 0) {
        printf("  ✗ Failed to install hooks/load agent\n");
        kill(agent_pid, SIGTERM);
        waitpid(agent_pid, nullptr, 0);
        frida_controller_destroy(controller);
        FAIL() << "Failed to install hooks/load agent";
        return;
    }
    
    // 5. Resume process
    printf("  5. Resuming process...\n");
    result = frida_controller_resume(controller);
    ASSERT_EQ(result, 0);
    
    // 6. Let it run briefly to see agent output
    printf("  6. Running for 2 seconds to observe agent logs...\n");
    sleep(2);
    
    // 7. Check shared memory to verify agent connected
    printf("  7. Verifying agent connected via shared memory...\n");
    uint32_t session_id = shared_memory_get_session_id();
    uint32_t controller_pid = shared_memory_get_pid();

    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "ada_control_%u_%08x", controller_pid, session_id);

    SharedMemoryRef shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, controller_pid, session_id, 4096);
    if (shm_control) {
        printf("  ✓ Agent successfully opened control shared memory\n");
        shared_memory_destroy(shm_control);
    } else {
        printf("  ⚠️  Could not verify agent shared memory connection\n");
    }
    
    // 8. Get statistics
    TracerStats stats = frida_controller_get_stats(controller);
    printf("  8. Stats - Events captured: %llu\n", stats.events_captured);
    
    // Clean up
    printf("  9. Cleaning up...\n");
    kill(agent_pid, SIGTERM);
    waitpid(agent_pid, nullptr, 0);
    frida_controller_destroy(controller);
    
    printf("  ✓ Agent loader test completed\n");
}

// Test: agent_loader__missing_library__then_error_reported
// Direct translation from the C test
TEST_F(AgentLoaderTest, agent_loader__missing_library__then_error_reported) {
    printf("[LOADER] missing_library → error reported\n");
    
    // Set environment to force bad path
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    // Spawn test program
    char* argv[] = {(char*)"test_cli", nullptr};
    uint32_t pid;
    
    int result = frida_controller_spawn_suspended(controller,
        ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli",
        argv, &pid);
    
    if (result != 0) {
        printf("  ⚠️  Spawn failed\n");
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Spawn failed";
        return;
    }
    
    // Attach
    result = frida_controller_attach(controller, pid);
    ASSERT_EQ(result, 0);
    
    // Try to install hooks - should fail
    printf("  Testing missing library handling...\n");
    
    const char* old_rpath = getenv("ADA_AGENT_RPATH_SEARCH_PATHS");
    setenv("ADA_AGENT_RPATH_SEARCH_PATHS", "/nonexistent", 1);
    result = frida_controller_install_hooks(controller);
    if (old_rpath) {
        setenv("ADA_AGENT_RPATH_SEARCH_PATHS", old_rpath, 1);
    } else {
        unsetenv("ADA_AGENT_RPATH_SEARCH_PATHS");
    }
    
    if (result != 0) {
        printf("  ✓ Correctly reported missing library error\n");
        SUCCEED();
    } else {
        printf("  ✗ Should have failed with missing library\n");
        ADD_FAILURE() << "Should have failed with missing library";
    }
    
    // Clean up
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    frida_controller_destroy(controller);
    
    printf("  ✓ Error handling test completed\n");
}
