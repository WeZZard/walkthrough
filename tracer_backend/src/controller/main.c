#include <tracer_backend/controller/frida_controller.h>
#include <tracer_backend/controller/cli_usage.h>
#include <tracer_backend/timer/timer.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static FridaController* g_controller = NULL;
static volatile bool g_running = true;
static bool g_timer_initialized = false;
static bool g_timer_started = false;

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
    timer_cancel();
}

void shutdown_initiate(void) {
    printf("\nDuration elapsed, initiating shutdown...\n");
    g_running = false;
    timer_cancel();
}

void print_usage(const char* program) {
    char usage_buffer[1024];
    size_t written = tracer_controller_format_usage(usage_buffer, sizeof(usage_buffer), program);

    if (written == 0) {
        fprintf(stdout, "Usage: %s <mode> <target> [options]\n", program);
        return;
    }

    fputs(usage_buffer, stdout);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    int exit_code = 0;

    const char* mode = argv[1];
    const char* target = argv[2];
    const char* output_dir = "./traces";
    const char* exclude_csv = NULL;
    double duration_seconds = 0.0;
    bool duration_specified = false;
    
    // Parse options
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
            exclude_csv = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            const char* value = argv[++i];
            char* endptr = NULL;
            double parsed = strtod(value, &endptr);
            if (endptr == value || parsed < 0.0) {
                fprintf(stderr, "Invalid duration '%s'. Expected non-negative value.\n", value);
                exit_code = 1;
                goto cleanup;
            }
            duration_seconds = parsed;
            duration_specified = true;
        }
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== ADA Tracer POC ===\n");
    printf("Output directory: %s\n", output_dir);
    if (exclude_csv) {
        printf("Exclude symbols: %s\n", exclude_csv);
        setenv("ADA_EXCLUDE", exclude_csv, 1);
    }

    if (timer_init() != 0) {
        fprintf(stderr, "Failed to initialize duration timer\n");
        exit_code = 1;
        goto cleanup;
    }
    g_timer_initialized = true;

    // Create output directory
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", output_dir);
    system(cmd);
    
    // Create controller
    g_controller = frida_controller_create(output_dir);
    if (!g_controller) {
        fprintf(stderr, "Failed to create controller\n");
        exit_code = 1;
        goto cleanup;
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
            if (strcmp(argv[i], "--duration") == 0) {
                i++; // Skip duration value
                continue;
            }
            spawn_argv[i - 2] = argv[i];
        }
        
        printf("Spawning process: %s\n", target);
        if (frida_controller_spawn_suspended(g_controller, target, spawn_argv, &pid) != 0) {
            fprintf(stderr, "Failed to spawn process\n");
            free(spawn_argv);
            exit_code = 1;
            goto cleanup;
        }

        free(spawn_argv);
        printf("Process spawned with PID: %u (suspended)\n", pid);
        
        // Attach to spawned process
        printf("Attaching to PID %u...\n", pid);
        if (frida_controller_attach(g_controller, pid) != 0) {
            fprintf(stderr, "Failed to attach to process\n");
            exit_code = 1;
            goto cleanup;
        }

    } else if (strcmp(mode, "attach") == 0) {
        pid = atoi(target);
        if (pid == 0) {
            fprintf(stderr, "Invalid PID: %s\n", target);
            exit_code = 1;
            goto cleanup;
        }

        printf("Attaching to PID %u...\n", pid);
        if (frida_controller_attach(g_controller, pid) != 0) {
            fprintf(stderr, "Failed to attach to process\n");
            exit_code = 1;
            goto cleanup;
        }

    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        exit_code = 1;
        goto cleanup;
    }

    printf("Successfully attached to PID %u\n", pid);

    // Install hooks
    printf("Installing hooks...\n");
    if (frida_controller_install_hooks(g_controller) != 0) {
        fprintf(stderr, "Failed to install hooks\n");
        exit_code = 1;
        goto cleanup;
    }

    printf("Hooks installed successfully\n");

    // Resume process if spawned
    if (strcmp(mode, "spawn") == 0) {
        printf("Resuming process...\n");
        if (frida_controller_resume(g_controller) != 0) {
            fprintf(stderr, "Failed to resume process\n");
            exit_code = 1;
            goto cleanup;
        }
        printf("Process resumed\n");
    }

    if (duration_specified && duration_seconds > 0.0) {
        long long rounded = llround(duration_seconds * 1000.0);
        if (rounded <= 0) {
            rounded = 1;
        }
        uint64_t duration_ms = (uint64_t)rounded;
        if (timer_start(duration_ms) != 0) {
            fprintf(stderr, "Failed to start duration timer\n");
            exit_code = 1;
            goto cleanup;
        }
        g_timer_started = true;
        printf("Duration timer armed for %.2f seconds\n", duration_ms / 1000.0);
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
                   stats.bytes_written, 0ULL /* drain_cycles removed */);
        }
        
        // Check if process is still running
        ProcessState state = frida_controller_get_state(g_controller);
        if (state != PROCESS_STATE_RUNNING && state != PROCESS_STATE_ATTACHED) {
            printf("Process has terminated\n");
            break;
        }

        if (duration_specified && g_timer_started && !timer_is_active()) {
            printf("Duration expired\n");
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
    // printf("Drain cycles:    %llu\n", final_stats.drain_cycles); // Field removed
    
    // Cleanup
cleanup:
    if (g_timer_initialized) {
        if (g_timer_started && timer_is_active()) {
            timer_cancel();
        }
        timer_cleanup();
        g_timer_initialized = false;
        g_timer_started = false;
    }

    if (g_controller != NULL) {
        frida_controller_destroy(g_controller);
        g_controller = NULL;
    }

    if (exit_code == 0) {
        printf("\nTracer POC completed successfully\n");
    }

    return exit_code;
}
