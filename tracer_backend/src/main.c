#include "frida_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static FridaController* g_controller = NULL;
static volatile bool g_running = true;

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
}

void print_usage(const char* program) {
    printf("Usage: %s <mode> <target> [options]\n", program);
    printf("\nModes:\n");
    printf("  spawn    - Spawn and trace a new process\n");
    printf("  attach   - Attach to an existing process\n");
    printf("\nExamples:\n");
    printf("  %s spawn ./test_cli --wait\n", program);
    printf("  %s spawn ./test_runloop\n", program);
    printf("  %s attach 1234\n", program);
    printf("\nOptions:\n");
    printf("  --output <dir>   - Output directory for traces (default: ./traces)\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* target = argv[2];
    const char* output_dir = "./traces";
    
    // Parse options
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== ADA Tracer POC ===\n");
    printf("Output directory: %s\n", output_dir);
    
    // Create output directory
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", output_dir);
    system(cmd);
    
    // Create controller
    g_controller = frida_controller_create(output_dir);
    if (!g_controller) {
        fprintf(stderr, "Failed to create controller\n");
        return 1;
    }
    
    uint32_t pid = 0;
    
    if (strcmp(mode, "spawn") == 0) {
        // Build argv for spawned process
        char** spawn_argv = calloc(argc - 2, sizeof(char*));
        spawn_argv[0] = (char*)target;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--output") == 0) {
                i++; // Skip output dir
                continue;
            }
            spawn_argv[i - 2] = argv[i];
        }
        
        printf("Spawning process: %s\n", target);
        if (frida_controller_spawn_suspended(g_controller, target, spawn_argv, &pid) != 0) {
            fprintf(stderr, "Failed to spawn process\n");
            free(spawn_argv);
            frida_controller_destroy(g_controller);
            return 1;
        }
        
        free(spawn_argv);
        printf("Process spawned with PID: %u (suspended)\n", pid);
        
        // Attach to spawned process
        printf("Attaching to PID %u...\n", pid);
        if (frida_controller_attach(g_controller, pid) != 0) {
            fprintf(stderr, "Failed to attach to process\n");
            frida_controller_destroy(g_controller);
            return 1;
        }
        
    } else if (strcmp(mode, "attach") == 0) {
        pid = atoi(target);
        if (pid == 0) {
            fprintf(stderr, "Invalid PID: %s\n", target);
            frida_controller_destroy(g_controller);
            return 1;
        }
        
        printf("Attaching to PID %u...\n", pid);
        if (frida_controller_attach(g_controller, pid) != 0) {
            fprintf(stderr, "Failed to attach to process\n");
            frida_controller_destroy(g_controller);
            return 1;
        }
        
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        frida_controller_destroy(g_controller);
        return 1;
    }
    
    printf("Successfully attached to PID %u\n", pid);
    
    // Install hooks
    printf("Installing hooks...\n");
    if (frida_controller_install_hooks(g_controller) != 0) {
        fprintf(stderr, "Failed to install hooks\n");
        frida_controller_destroy(g_controller);
        return 1;
    }
    
    printf("Hooks installed successfully\n");
    
    // Resume process if spawned
    if (strcmp(mode, "spawn") == 0) {
        printf("Resuming process...\n");
        if (frida_controller_resume(g_controller) != 0) {
            fprintf(stderr, "Failed to resume process\n");
            frida_controller_destroy(g_controller);
            return 1;
        }
        printf("Process resumed\n");
    }
    
    // Monitor loop
    printf("\n=== Tracing Active ===\n");
    printf("Press Ctrl+C to stop\n\n");
    
    int tick = 0;
    while (g_running) {
        sleep(1);
        tick++;
        
        // Print statistics every 5 seconds
        if (tick % 5 == 0) {
            TracerStats stats = frida_controller_get_stats(g_controller);
            printf("[Stats] Events: %llu, Dropped: %llu, Bytes: %llu, Cycles: %llu\n",
                   stats.events_captured, stats.events_dropped,
                   stats.bytes_written, stats.drain_cycles);
        }
        
        // Check if process is still running
        ProcessState state = frida_controller_get_state(g_controller);
        if (state != PROCESS_STATE_RUNNING && state != PROCESS_STATE_ATTACHED) {
            printf("Process has terminated\n");
            break;
        }
    }
    
    // Detach and cleanup
    printf("\nDetaching from process...\n");
    frida_controller_detach(g_controller);
    
    // Print final statistics
    TracerStats final_stats = frida_controller_get_stats(g_controller);
    printf("\n=== Final Statistics ===\n");
    printf("Events captured: %llu\n", final_stats.events_captured);
    printf("Events dropped:  %llu\n", final_stats.events_dropped);
    printf("Bytes written:   %llu\n", final_stats.bytes_written);
    printf("Drain cycles:    %llu\n", final_stats.drain_cycles);
    
    // Cleanup
    frida_controller_destroy(g_controller);
    
    printf("\nTracer POC completed successfully\n");
    return 0;
}