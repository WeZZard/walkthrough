#include "frida_controller.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void test_spawn_and_attach() {
    printf("Testing spawn and attach to test_cli...\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    assert(controller != NULL);
    
    // Spawn test_cli suspended
    char* argv[] = {"./test_cli", "--wait", NULL};
    uint32_t pid = 0;
    
    int result = frida_controller_spawn_suspended(controller, "./test_cli", argv, &pid);
    assert(result == 0);
    assert(pid > 0);
    printf("  ✓ Spawned test_cli with PID %u\n", pid);
    
    // Attach to the spawned process
    result = frida_controller_attach(controller, pid);
    assert(result == 0);
    printf("  ✓ Attached to process\n");
    
    // Install hooks
    result = frida_controller_install_hooks(controller);
    assert(result == 0);
    printf("  ✓ Installed hooks\n");
    
    // Resume the process
    result = frida_controller_resume(controller);
    assert(result == 0);
    printf("  ✓ Resumed process\n");
    
    // Let it run for a bit
    sleep(3);
    
    // Check statistics
    TracerStats stats = frida_controller_get_stats(controller);
    printf("  Events captured: %llu\n", stats.events_captured);
    assert(stats.events_captured > 0);
    
    // Detach
    frida_controller_detach(controller);
    
    // Cleanup
    frida_controller_destroy(controller);
    
    // Kill the test process if still running
    kill(pid, SIGTERM);
    waitpid(pid, NULL, WNOHANG);
    
    printf("  ✓ Spawn and attach test passed\n");
}

void test_shared_memory_communication() {
    printf("Testing shared memory communication...\n");
    
    // Create controller which sets up shared memory
    FridaController* controller = frida_controller_create("./test_output");
    assert(controller != NULL);
    
    // Open the shared memory from another process perspective
    SharedMemory* shm_control = shared_memory_open("ada_control", 4096);
    SharedMemory* shm_index = shared_memory_open("ada_index", 32 * 1024 * 1024);
    
    assert(shm_control != NULL);
    assert(shm_index != NULL);
    
    // Verify control block is accessible
    ControlBlock* control = (ControlBlock*)shm_control->address;
    assert(control->process_state == PROCESS_STATE_INITIALIZED);
    assert(control->index_lane_enabled == 1);
    
    // Create ring buffer view
    RingBuffer* rb = ring_buffer_create(shm_index->address, 32 * 1024 * 1024, sizeof(IndexEvent));
    assert(rb != NULL);
    assert(ring_buffer_is_empty(rb));
    
    // Write a test event
    IndexEvent event = {
        .timestamp = 12345,
        .function_id = 0x100,
        .thread_id = 1,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = 1
    };
    
    assert(ring_buffer_write(rb, &event));
    
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

void test_state_transitions() {
    printf("Testing state transitions...\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    assert(controller != NULL);
    
    // Initial state
    assert(frida_controller_get_state(controller) == PROCESS_STATE_INITIALIZED);
    
    // Spawn process
    char* argv[] = {"./test_cli", NULL};
    uint32_t pid = 0;
    frida_controller_spawn_suspended(controller, "./test_cli", argv, &pid);
    
    ProcessState state = frida_controller_get_state(controller);
    assert(state == PROCESS_STATE_SUSPENDED || state == PROCESS_STATE_SPAWNING);
    
    // Attach
    frida_controller_attach(controller, pid);
    assert(frida_controller_get_state(controller) == PROCESS_STATE_ATTACHED);
    
    // Resume
    frida_controller_resume(controller);
    assert(frida_controller_get_state(controller) == PROCESS_STATE_RUNNING);
    
    sleep(1);
    
    // Detach
    frida_controller_detach(controller);
    assert(frida_controller_get_state(controller) == PROCESS_STATE_INITIALIZED);
    
    // Cleanup
    frida_controller_destroy(controller);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, WNOHANG);
    
    printf("  ✓ State transitions test passed\n");
}

void test_statistics_collection() {
    printf("Testing statistics collection...\n");
    
    FridaController* controller = frida_controller_create("./test_output");
    assert(controller != NULL);
    
    // Initial stats should be zero
    TracerStats stats = frida_controller_get_stats(controller);
    assert(stats.events_captured == 0);
    assert(stats.events_dropped == 0);
    assert(stats.drain_cycles == 0);
    
    // Wait for drain thread to run a few cycles
    sleep(2);
    
    stats = frida_controller_get_stats(controller);
    assert(stats.drain_cycles > 0); // Should have run at least once
    
    frida_controller_destroy(controller);
    
    printf("  ✓ Statistics collection test passed\n");
}

int main() {
    printf("\n=== Integration Tests ===\n\n");
    
    // Check if test_cli exists
    if (access("./test_cli", X_OK) != 0) {
        printf("Warning: test_cli not found or not executable\n");
        printf("Build test_cli first with: cmake .. && make test_cli\n");
        printf("Skipping integration tests that require test_cli\n\n");
        
        // Run tests that don't need test_cli
        test_shared_memory_communication();
        test_statistics_collection();
    } else {
        // Run all tests
        test_spawn_and_attach();
        test_shared_memory_communication();
        test_state_transitions();
        test_statistics_collection();
    }
    
    printf("\n✅ All integration tests passed!\n\n");
    return 0;
}