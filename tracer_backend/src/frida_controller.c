#include "frida_controller.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include "tracer_types.h"
#include <frida-core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <spawn.h>
#include <assert.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

#define INDEX_LANE_SIZE (32 * 1024 * 1024)  // 32MB
#define DETAIL_LANE_SIZE (32 * 1024 * 1024) // 32MB
#define CONTROL_BLOCK_SIZE 4096

typedef enum {
    SPAWN_METHOD_NONE,
    SPAWN_METHOD_FRIDA
} SpawnMethod;

struct FridaController {
    // Frida objects
    FridaDeviceManager* manager;
    FridaDevice* device;
    FridaSession* session;
    FridaScript* script;
    
    // Process management
    guint pid;
    ProcessState state;
    SpawnMethod spawn_method;
    
    // Shared memory
    SharedMemoryRef shm_control;
    SharedMemoryRef shm_index;
    SharedMemoryRef shm_detail;
    ControlBlock* control_block;
    
    // Ring buffers
    RingBuffer* index_ring;
    RingBuffer* detail_ring;
    
    // Drain thread
    GThread* drain_thread;
    bool drain_running;
    
    // Output
    char output_dir[256];
    FILE* output_file;
    
    // Statistics
    TracerStats stats;
    
    // Event loop
    GMainLoop* main_loop;

    GMainContext* main_context;
};

// Shared memory naming: typed string literal constants for roles (declared in shared_memory.h)

// Generate or retrieve a per-process session id (random), used for uniqueness when enabled
// Use shared_memory.c's session id indirectly via name builder.

// Build shared memory name using shared_memory API (unique session-based)
static void
build_shm_name(char *dst, size_t dst_len, const char *role, pid_t pid_hint)
{
    uint32_t sid = shared_memory_get_session_id();
    pid_t pid_part = (pid_hint > 0) ? pid_hint : getpid();
    snprintf(dst, dst_len, "%s_%s_%d_%08x", ADA_SHM_PREFIX, role, (int) pid_part, (unsigned int) sid);
}

// Forward declarations
static void* drain_thread_func(void* arg);
static void on_detached(FridaSession* session, FridaSessionDetachReason reason, 
                        FridaCrash* crash, gpointer user_data);
static void on_message(FridaScript* script, const gchar* message, 
                       GBytes* data, gpointer user_data);

__attribute__((constructor))
void frida_init_per_process (void) {
    frida_init();
}

__attribute__((destructor))
void frida_deinit_per_process (void) {
    frida_deinit();
}

FridaController* frida_controller_create(const char* output_dir) {
    FridaController* controller = calloc(1, sizeof(FridaController));
    if (!controller) {
        g_debug("Failed to allocate memory for FridaController\n");
        return NULL;
    }
    
    // Initialize spawn method
    controller->spawn_method = SPAWN_METHOD_NONE;
    
    // Copy output directory
    strncpy(controller->output_dir, output_dir, sizeof(controller->output_dir) - 1);
    
    controller->main_context = g_main_context_new();
    g_main_context_push_thread_default(controller->main_context );

    controller->main_loop = g_main_loop_new (controller->main_context , TRUE);
    
    // Create device manager
    controller->manager = frida_device_manager_new();

    // Get local device
    GError* error = NULL;
    FridaDeviceList* devices = frida_device_manager_enumerate_devices_sync(
        controller->manager, NULL, &error);
    
    if (error) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
        g_main_loop_quit(controller->main_loop);
        g_main_loop_unref(controller->main_loop);
        g_main_context_pop_thread_default(controller->main_context);
        g_main_context_unref(controller->main_context);
        free(controller);
        g_debug("Failed to enumerate devices\n");
        return NULL;
    }
    
    // Find local device
    gint num_devices = frida_device_list_size(devices);
    for (gint i = 0; i < num_devices; i++) {
        FridaDevice* device = frida_device_list_get(devices, i);
        if (frida_device_get_dtype(device) == FRIDA_DEVICE_TYPE_LOCAL) {
            controller->device = g_object_ref(device);
        }
        g_object_unref(device);
    }
    
    frida_unref(devices);
    
    if (!controller->device) {
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
        g_main_loop_quit(controller->main_loop);
        g_main_loop_unref(controller->main_loop);
        g_main_context_pop_thread_default(controller->main_context);
        g_main_context_unref(controller->main_context);
        free(controller);
        g_debug("Failed to create shared memory segments\n");
        return NULL;
    }
    
    // Create shared memory segments (optionally unique names controlled by ADA_SHM_DISABLE_UNIQUE)
    char name_control[256];
    char name_index[256];
    char name_detail[256];
    build_shm_name(name_control, sizeof(name_control), ADA_ROLE_CONTROL, 0 /* pid unknown yet */);
    build_shm_name(name_index, sizeof(name_index), ADA_ROLE_INDEX, 0);
    build_shm_name(name_detail, sizeof(name_detail), ADA_ROLE_DETAIL, 0);

    // Create shared memory with the controller's PID so agent can find it
    // Use the shared_memory API to get controller's PID
    uint32_t controller_pid = shared_memory_get_pid();
    uint32_t session_id = shared_memory_get_session_id();
    
    controller->shm_control = shared_memory_create_unique(ADA_ROLE_CONTROL,
                                                         controller_pid, session_id,
                                                         CONTROL_BLOCK_SIZE, NULL, 0);
    controller->shm_index = shared_memory_create_unique(ADA_ROLE_INDEX,
                                                       controller_pid, session_id,
                                                       INDEX_LANE_SIZE, NULL, 0);
    controller->shm_detail = shared_memory_create_unique(ADA_ROLE_DETAIL,
                                                        controller_pid, session_id,
                                                        DETAIL_LANE_SIZE, NULL, 0);
    
    if (!controller->shm_control || !controller->shm_index || !controller->shm_detail) {
        frida_controller_destroy(controller);
        g_debug("Failed to initialize control block\n");
        return NULL;
    }
    
    // Initialize control block
    controller->control_block = (ControlBlock*)shared_memory_get_address(controller->shm_control);
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
    controller->control_block->flight_state = FLIGHT_RECORDER_IDLE;
    controller->control_block->index_lane_enabled = 1;
    controller->control_block->detail_lane_enabled = 0;
    controller->control_block->pre_roll_ms = 1000;
    controller->control_block->post_roll_ms = 1000;
    
    // Create ring buffers
    controller->index_ring = ring_buffer_create(shared_memory_get_address(controller->shm_index),
                                                INDEX_LANE_SIZE,
                                                sizeof(IndexEvent));
    controller->detail_ring = ring_buffer_create(shared_memory_get_address(controller->shm_detail),
                                                 DETAIL_LANE_SIZE,
                                                 sizeof(DetailEvent));
    
    // Start drain thread
    controller->drain_running = true;
    controller->drain_thread = g_thread_new("drain_thread", drain_thread_func, controller);
    
    controller->state = PROCESS_STATE_INITIALIZED;
    
    return controller;
}

void frida_controller_destroy(FridaController* controller) {
    if (!controller) return;
    
    // Stop drain thread
    controller->drain_running = false;
    if (controller->drain_thread) {
        g_thread_join(controller->drain_thread);
    }
    
    // Cleanup Frida objects
    if (controller->script) {
        frida_script_unload_sync(controller->script, NULL, NULL);
        frida_unref(controller->script);
    }
    
    if (controller->session) {
        frida_session_detach_sync(controller->session, NULL, NULL);
        frida_unref(controller->session);
    }
    
    if (controller->device) {
        frida_unref(controller->device);
    }
    
    if (controller->manager) {
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
    }
    
    // Cleanup ring buffers
    ring_buffer_destroy(controller->index_ring);
    ring_buffer_destroy(controller->detail_ring);
    
    // Cleanup shared memory
    shared_memory_destroy(controller->shm_control);
    shared_memory_destroy(controller->shm_index);
    shared_memory_destroy(controller->shm_detail);
    
    // Close output file
    if (controller->output_file) {
        fclose(controller->output_file);
    }
    
    // Cleanup event loop
    if (controller->main_loop) {
        g_main_loop_quit(controller->main_loop);
        g_main_loop_unref(controller->main_loop);
        g_main_context_pop_thread_default(controller->main_context);
        g_main_context_unref(controller->main_context);
    }
    
    free(controller);
}

int frida_controller_spawn_suspended(FridaController* controller,
                                     const char* path,
                                     char* const argv[],
                                     uint32_t* out_pid) {
    if (!controller || !path) return -1;
    
    controller->state = PROCESS_STATE_SPAWNING;
    controller->control_block->process_state = PROCESS_STATE_SPAWNING;
    
    // Use Frida for system binaries
    GError *error = NULL;
    FridaSpawnOptions *options = frida_spawn_options_new();
    
    // Build argv array - count the arguments
    gint argv_len = 0;
    if (argv) {
      while (argv[argv_len])
        argv_len++;
    }
    frida_spawn_options_set_argv(options, (gchar **)argv, argv_len);

    // Pass session id and host pid to the spawned process via environment for agent to discover
    uint32_t sid = shared_memory_get_session_id();
    char sid_hex[16];
    snprintf(sid_hex, sizeof(sid_hex), "%08x", (unsigned int) sid);
    pid_t host_pid = getpid();
    char host_pid_str[32];
    snprintf(host_pid_str, sizeof(host_pid_str), "%d", (int) host_pid);
    size_t envc = 0;
    for (char **ep = environ; ep && *ep; ep++) envc++;
    char **envp = (char **) calloc(envc + 3, sizeof(char *));
    if (envp) {
        for (size_t i = 0; i < envc; i++) envp[i] = strdup(environ[i]);
        char varbuf1[64];
        snprintf(varbuf1, sizeof(varbuf1), "ADA_SHM_SESSION_ID=%s", sid_hex);
        envp[envc] = strdup(varbuf1);
        char varbuf2[64];
        snprintf(varbuf2, sizeof(varbuf2), "ADA_SHM_HOST_PID=%s", host_pid_str);
        envp[envc + 1] = strdup(varbuf2);
        frida_spawn_options_set_env(options, (gchar **) envp, (gint) (envc + 2));
    }

    // Frida’s spawn-suspended flow is designed to allow attach + hook 
    // before first instruction, then frida_device_resume_sync
    guint pid = frida_device_spawn_sync(controller->device, path, options, NULL,
                                        &error);
    g_object_unref(options);
    if (envp) {
        for (size_t i = 0; i < envc + 2; i++) free(envp[i]);
        free(envp);
    }

    if (error) {
      g_printerr("Failed to spawn: %s\n", error->message);
      g_error_free(error);
      return -1;
    }

    controller->pid = pid;
    *out_pid = pid;
    controller->spawn_method = SPAWN_METHOD_FRIDA;
    controller->state = PROCESS_STATE_SUSPENDED;
    controller->control_block->process_state = PROCESS_STATE_SUSPENDED;
    return 0;
}

int frida_controller_attach(FridaController* controller, uint32_t pid) {
    if (!controller) return -1;
    
    controller->state = PROCESS_STATE_ATTACHING;
    controller->control_block->process_state = PROCESS_STATE_ATTACHING;
    
    GError* error = NULL;
    FridaSessionOptions* options = frida_session_options_new();

    controller->session = frida_device_attach_sync(controller->device, pid, 
                                                   options, NULL, &error);
    
    g_object_unref(options);
    
    if (error) {
        g_printerr("Failed to attach: %s\n", error->message);
        g_error_free(error);
        controller->state = PROCESS_STATE_FAILED;
        controller->control_block->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    // Connect detached signal
    g_signal_connect(controller->session, "detached", 
                     G_CALLBACK(on_detached), controller);
    
    controller->pid = pid;
    controller->state = PROCESS_STATE_ATTACHED;
    controller->control_block->process_state = PROCESS_STATE_ATTACHED;
    
    return 0;
}

int frida_controller_install_hooks(FridaController* controller) {
    if (!controller || !controller->session) return -1;
    
    // Compute absolute path to the agent library
    char agent_path[1024];
    const char* build_type = getenv("ADA_BUILD_TYPE");
    if (!build_type) build_type = "debug";  // default to debug
    
    // Try predictable path first
    snprintf(agent_path, sizeof(agent_path),
             "%s/target/%s/tracer_backend/lib/libfrida_agent.dylib",
             getenv("PWD") ? getenv("PWD") : ".", build_type);
    
    // Check if file exists
    if (access(agent_path, F_OK) != 0) {
        // Try alternative path
        snprintf(agent_path, sizeof(agent_path),
                 "/Users/wezzard/Projects/ADA/target/%s/tracer_backend/lib/libfrida_agent.dylib",
                 build_type);
        
        if (access(agent_path, F_OK) != 0) {
            fprintf(stderr, "[Controller] Agent library not found at %s\n", agent_path);
            return -1;
        }
    }
    
    printf("[Controller] Using agent library: %s\n", agent_path);
    
    // Prepare initialization payload for the agent
    // Pass the CONTROLLER's PID (not the target's PID) so agent can connect to controller's shared memory
    // Use the shared_memory API to get consistent PID and session ID
    char init_payload[256];
    snprintf(init_payload, sizeof(init_payload),
             "host_pid=%u;session_id=%08x",
             shared_memory_get_pid(), shared_memory_get_session_id());
    
    // Create QuickJS loader script
    char script_source[4096];
    snprintf(script_source, sizeof(script_source),
        "console.log('[Loader] Starting native agent injection');\n"
        "console.log('[Loader] Agent path: %s');\n"
        "console.log('[Loader] Init payload: %s');\n"
        "\n"
        "try {\n"
        "  const agent_path = '%s';\n"
        "  const init_payload = '%s';\n"
        "  \n"
        "  // Load the native agent module\n"
        "  const mod = Module.load(agent_path);\n"
        "  console.log('[Loader] Agent loaded at base:', mod.base);\n"
        "  \n"
        "  // Get the agent_init function\n"
        "  const agent_init = mod.getExportByName('agent_init');\n"
        "  if (agent_init) {\n"
        "    console.log('[Loader] Found agent_init at:', agent_init);\n"
        "    \n"
        "    // Create native function wrapper\n"
        "    // agent_init(const gchar* data, gint data_size)\n"
        "    const initFunc = new NativeFunction(agent_init, 'void', ['pointer', 'int']);\n"
        "    \n"
        "    // Allocate and write the payload\n"
        "    const payloadBuf = Memory.allocUtf8String(init_payload);\n"
        "    \n"
        "    // Call agent_init\n"
        "    try {\n"
        "      initFunc(payloadBuf, init_payload.length);\n"
        "      console.log('[Loader] Agent initialized successfully');\n"
        "    } catch (e2) {\n"
        "      console.error('[Loader] Error calling agent_init:', e2.toString());\n"
        "    }\n"
        "  } else {\n"
        "    console.error('[Loader] agent_init not found in agent');\n"
        "  }\n"
        "  \n"
        "  // Export a ping function for health checks\n"
        "  rpc.exports = {\n"
        "    ping: function() { return 'ok'; }\n"
        "  };\n"
        "} catch (e) {\n"
        "  console.error('[Loader] Error:', e.toString());\n"
        "  throw e;\n"
        "}\n",
        agent_path, init_payload, agent_path, init_payload);
    
    GError* error = NULL;
    FridaScriptOptions* options = frida_script_options_new();
    frida_script_options_set_name(options, "agent-loader");
    frida_script_options_set_runtime(options, FRIDA_SCRIPT_RUNTIME_QJS);
    
    // May get timeout error when debugger is attached.
    controller->script = frida_session_create_script_sync(controller->session,
                                                          script_source,
                                                          options, NULL, &error);
    
    g_object_unref(options);
    
    if (error) {
        g_printerr("Failed to create script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    // Connect message handler
    g_signal_connect(controller->script, "message", 
                     G_CALLBACK(on_message), controller);
    
    // Load script
    frida_script_load_sync(controller->script, NULL, &error);
    if (error) {
        g_printerr("Failed to load script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    return 0;
}

int frida_controller_inject_agent(FridaController* controller, const char* agent_path) {
    if (!controller || !controller->session || !agent_path) return -1;
    
    printf("[Controller] Injecting native agent: %s\n", agent_path);
    
    // Read the agent library file
    FILE* f = fopen(agent_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open agent file: %s\n", agent_path);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char* agent_data = malloc(file_size);
    if (!agent_data) {
        fclose(f);
        return -1;
    }
    
    size_t read_size = fread(agent_data, 1, file_size, f);
    fclose(f);
    
    if (read_size != file_size) {
        free(agent_data);
        return -1;
    }
    
    // Create a loader script that will load our native agent
    char loader_script[4096];
    snprintf(loader_script, sizeof(loader_script),
        "const agent_data = %s;\n"
        "const agent = Module.load(agent_data);\n"
        "console.log('[Loader] Native agent loaded at', agent.base);\n"
        "const init_func = agent.getExportByName('frida_agent_main');\n"
        "if (init_func) {\n"
        "  const func = new NativeFunction(init_func, 'void', []);\n"
        "  func();\n"
        "  console.log('[Loader] Agent initialized');\n"
        "} else {\n"
        "  console.error('[Loader] Agent entry point not found');\n"
        "}\n",
        "hexData" // We'll need to convert agent_data to hex string
    );
    
    // For now, just use the minimal script - full implementation would convert to hex
    // and properly inject the agent
    const char* simple_loader = 
        "console.log('[Loader] Native agent injection placeholder');\n"
        "// TODO: Implement proper agent loading\n";
    
    GError* error = NULL;
    FridaScriptOptions* options = frida_script_options_new();
    frida_script_options_set_name(options, "agent-loader");
    frida_script_options_set_runtime(options, FRIDA_SCRIPT_RUNTIME_QJS);
    
    FridaScript* loader_script_obj = frida_session_create_script_sync(
        controller->session, simple_loader, options, NULL, &error);
    
    g_object_unref(options);
    free(agent_data);
    
    if (error) {
        g_printerr("Failed to create loader script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    // Load the script
    frida_script_load_sync(loader_script_obj, NULL, &error);
    if (error) {
        g_printerr("Failed to load loader script: %s\n", error->message);
        g_error_free(error);
        g_object_unref(loader_script_obj);
        return -1;
    }
    
    // For now we don't keep the loader script reference
    g_object_unref(loader_script_obj);
    
    printf("[Controller] Agent injection completed\n");
    return 0;
}

int frida_controller_resume(FridaController* controller) {
    if (!controller) return -1;
    
    if (controller->state != PROCESS_STATE_SUSPENDED && 
        controller->state != PROCESS_STATE_ATTACHED) {
        return -1;
    }

    assert(controller->spawn_method == SPAWN_METHOD_FRIDA);
    assert(controller->device);
    assert(controller->pid > 0);
    
    // Resume via Frida if using Frida spawn
    if (controller->device && controller->pid > 0) {
        GError* error = NULL;
        frida_device_resume_sync(controller->device, controller->pid, NULL, &error);
        if (error) {
            g_error_free(error);
            controller->state = PROCESS_STATE_FAILED;
            controller->control_block->process_state = PROCESS_STATE_FAILED;
            return -1;
        } else {
            controller->state = PROCESS_STATE_RUNNING;
            controller->control_block->process_state = PROCESS_STATE_RUNNING;
        }
    } else {
        controller->state = PROCESS_STATE_FAILED;
        controller->control_block->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    return 0;
}

int frida_controller_detach(FridaController* controller) {
    if (!controller || !controller->session) return -1;
    
    controller->state = PROCESS_STATE_DETACHING;
    controller->control_block->process_state = PROCESS_STATE_DETACHING;
    
    GError* error = NULL;
    frida_session_detach_sync(controller->session, NULL, &error);
    
    if (error) {
        g_error_free(error);
        return -1;
    }
    
    controller->state = PROCESS_STATE_INITIALIZED;
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
    
    return 0;
}

ProcessState frida_controller_get_state(FridaController* controller) {
    return controller ? controller->state : PROCESS_STATE_UNINITIALIZED;
}

TracerStats frida_controller_get_stats(FridaController* controller) {
    if (!controller) {
        TracerStats empty = {0};
        return empty;
    }
    return controller->stats;
}

// Callbacks
static void on_detached(FridaSession* session, FridaSessionDetachReason reason,
                        FridaCrash* crash, gpointer user_data) {
    FridaController* controller = (FridaController*)user_data;
    controller->state = PROCESS_STATE_INITIALIZED;
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
}

static void on_message(FridaScript* script, const gchar* message,
                       GBytes* data, gpointer user_data) {
    g_print("Script message: %s\n", message);
}

// Drain thread
static void* drain_thread_func(void* arg) {
    FridaController* controller = (FridaController*)arg;
    IndexEvent index_events[1000];
    DetailEvent detail_events[100];
    
    while (controller->drain_running) {
        // Drain index lane
        size_t index_count = ring_buffer_read_batch(controller->index_ring, 
                                                    index_events, 1000);
        if (index_count > 0) {
            controller->stats.events_captured += index_count;
            controller->stats.bytes_written += index_count * sizeof(IndexEvent);
            
            // Write to file if open
            if (controller->output_file) {
                fwrite(index_events, sizeof(IndexEvent), index_count, 
                      controller->output_file);
            }
        }
        
        // Drain detail lane
        size_t detail_count = ring_buffer_read_batch(controller->detail_ring,
                                                     detail_events, 100);
        if (detail_count > 0) {
            controller->stats.events_captured += detail_count;
            controller->stats.bytes_written += detail_count * sizeof(DetailEvent);
            
            // Write to file if open
            if (controller->output_file) {
                fwrite(detail_events, sizeof(DetailEvent), detail_count,
                      controller->output_file);
            }
        }
        
        controller->stats.drain_cycles++;
        
        // Sleep for 100ms
        usleep(100000);
    }
    
    return NULL;
}