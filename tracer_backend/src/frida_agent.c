#include <stdint.h>
#include <frida-gum.h>
#include "tracer_types.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <ctype.h>

#ifdef __APPLE__
#include <pthread/pthread.h>
#include <mach/thread_info.h>
#include <mach/mach_init.h>
#endif

typedef struct {
    GumInterceptor* interceptor;
    SharedMemoryRef shm_index;
    SharedMemoryRef shm_detail;
    SharedMemoryRef shm_control;
    RingBuffer* index_ring;
    RingBuffer* detail_ring;
    ControlBlock* control_block;
    guint num_hooks;
    guint64 module_base;
    guint32 host_pid;
    guint32 session_id;
} AgentContext;

typedef struct {
    AgentContext* ctx;
    guint64 function_id;
    const char* function_name;
} HookData;

static pthread_key_t g_tls_key;

// TLS for reentrancy guard
typedef struct {
    guint32 thread_id;
    guint32 call_depth;
    bool in_handler;
} ThreadLocalData;

static ThreadLocalData* get_thread_local() {
    ThreadLocalData* tls = pthread_getspecific(g_tls_key);
    if (!tls) {
        tls = calloc(1, sizeof(ThreadLocalData));
#ifdef __APPLE__
        tls->thread_id = pthread_mach_thread_np(pthread_self());
#else
        tls->thread_id = (uint32_t)(uintptr_t)pthread_self();
#endif
        pthread_setspecific(g_tls_key, tls);
    }
    return tls;
}

static guint64 get_timestamp() {
    return mach_absolute_time();
}

// Payload parser for agent_init data: accepts text like "host_pid=1234;session_id=89abcdef"
// Also accepts keys: pid/host_pid and sid/session_id; values may be decimal or hex (0x... or plain hex)
static void parse_init_payload(const gchar* data, gint data_size,
                               uint32_t* out_host_pid, uint32_t* out_session_id) {
    if (!data || data_size <= 0) return;
    // Copy to a null-terminated buffer
    char buf[256];
    size_t copy_len = (size_t) data_size < sizeof(buf) - 1 ? (size_t) data_size : sizeof(buf) - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';

    // Normalize separators to spaces
    for (size_t i = 0; i < copy_len; i++) {
        if (buf[i] == ';' || buf[i] == ',' || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') {
            buf[i] = ' ';
        }
    }

    // Tokenize by spaces
    char* saveptr = NULL;
    for (char* tok = strtok_r(buf, " ", &saveptr); tok != NULL; tok = strtok_r(NULL, " ", &saveptr)) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = tok;
        const char* val = eq + 1;
        if (val[0] == '\0') continue;

        if (strcmp(key, "host_pid") == 0 || strcmp(key, "pid") == 0) {
            *out_host_pid = (uint32_t) strtoul(val, NULL, 0);
        } else if (strcmp(key, "session_id") == 0 || strcmp(key, "sid") == 0) {
            // support raw hex without 0x prefix
            unsigned int parsed = 0;
            if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0) {
                parsed = (unsigned int) strtoul(val, NULL, 16);
            } else {
                // detect hex if contains [a-fA-F]
                gboolean looks_hex = FALSE;
                for (const char* p = val; *p; p++) {
                    if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) { looks_hex = TRUE; break; }
                }
                parsed = (unsigned int) strtoul(val, NULL, looks_hex ? 16 : 10);
            }
            *out_session_id = (uint32_t) parsed;
        }
    }
}

static void on_enter(GumInvocationContext* ic, gpointer user_data) {
    HookData* hook = (HookData*)user_data;
    AgentContext* ctx = hook->ctx;
    ThreadLocalData* tls = get_thread_local();
    
    // Reentrancy guard
    if (tls->in_handler) return;
    tls->in_handler = true;
    
    // Increment call depth
    tls->call_depth++;
    
    // Capture index event (always)
    if (ctx->control_block->index_lane_enabled) {
        IndexEvent event = {
            .timestamp = get_timestamp(),
            .function_id = hook->function_id,
            .thread_id = tls->thread_id,
            .event_kind = EVENT_KIND_CALL,
            .call_depth = tls->call_depth,
            ._padding = 0
        };
        
        ring_buffer_write(ctx->index_ring, &event);
    }
    
    // Capture detail event (if in window)
    if (ctx->control_block->detail_lane_enabled &&
        ctx->control_block->flight_state == FLIGHT_RECORDER_RECORDING) {
        
        DetailEvent detail = {0};
        detail.timestamp = get_timestamp();
        detail.function_id = hook->function_id;
        detail.thread_id = tls->thread_id;
        detail.event_kind = EVENT_KIND_CALL;
        detail.call_depth = tls->call_depth;
        
        #ifdef __aarch64__
        // Capture ARM64 registers
        GumCpuContext* cpu = ic->cpu_context;
        if (cpu) {
            for (int i = 0; i < 8; i++) {
                detail.x_regs[i] = cpu->x[i];
            }
            detail.lr = cpu->lr;
            detail.fp = cpu->fp;
            detail.sp = cpu->sp;
            
            // Capture stack snapshot (128 bytes from SP)
            void* sp_ptr = (void*)cpu->sp;
            size_t copy_size = 128;
            if (sp_ptr) {
                memcpy(detail.stack_snapshot, sp_ptr, copy_size);
                detail.stack_size = copy_size;
            }
        }
        #endif
        
        ring_buffer_write(ctx->detail_ring, &detail);
    }
    
    tls->in_handler = false;
}

static void on_leave(GumInvocationContext* ic, gpointer user_data) {
    HookData* hook = (HookData*)user_data;
    AgentContext* ctx = hook->ctx;
    ThreadLocalData* tls = get_thread_local();
    
    // Reentrancy guard
    if (tls->in_handler) return;
    tls->in_handler = true;
    
    // Capture index event (always)
    if (ctx->control_block->index_lane_enabled) {
        IndexEvent event = {
            .timestamp = get_timestamp(),
            .function_id = hook->function_id,
            .thread_id = tls->thread_id,
            .event_kind = EVENT_KIND_RETURN,
            .call_depth = tls->call_depth,
            ._padding = 0
        };
        
        ring_buffer_write(ctx->index_ring, &event);
    }
    
    // Capture detail event (if in window)
    if (ctx->control_block->detail_lane_enabled &&
        ctx->control_block->flight_state == FLIGHT_RECORDER_RECORDING) {
        
        DetailEvent detail = {0};
        detail.timestamp = get_timestamp();
        detail.function_id = hook->function_id;
        detail.thread_id = tls->thread_id;
        detail.event_kind = EVENT_KIND_RETURN;
        detail.call_depth = tls->call_depth;
        
        #ifdef __aarch64__
        // Capture return value in x0
        GumCpuContext* cpu = ic->cpu_context;
        detail.x_regs[0] = cpu->x[0];
        #endif
        
        ring_buffer_write(ctx->detail_ring, &detail);
    }
    
    // Decrement call depth
    if (tls->call_depth > 0) {
        tls->call_depth--;
    }
    
    tls->in_handler = false;
}

// Forward declaration
__attribute__((visibility("default")))
void agent_init(const gchar* data, gint data_size);

static uint32_t g_host_pid = UINT32_MAX;
static uint32_t g_session_id = UINT32_MAX;

AgentContext* get_shared_agent_context() {
    static gsize ctx_initialized = 0;
    static AgentContext* ctx = NULL;
    if (g_once_init_enter(&ctx_initialized)) {

        uint32_t session_id = g_session_id;
        uint32_t host_pid = g_host_pid;
    
        if (session_id == UINT32_MAX) {
            g_debug("No session id provided, trying environment\n"); 
            const char *sid_env = getenv("ADA_SHM_SESSION_ID");
            if (sid_env && sid_env[0] != '\0') {
                session_id = (uint32_t) strtoul(sid_env, NULL, 16);
            }
        } else {
            g_debug("Using provided session id: %u\n", session_id);
        }

        if (host_pid == UINT32_MAX) {
            g_debug("No host pid provided, trying environment\n"); 
            const char *host_env = getenv("ADA_SHM_HOST_PID");
            if (host_env && host_env[0] != '\0') {
                host_pid = (uint32_t) strtoul(host_env, NULL, 10);
            }
        } else {
            g_debug("Using provided host pid: %u\n", host_pid);
        }

        gboolean is_ready = TRUE;

        if (session_id == UINT32_MAX) {
            g_debug("Failed to resolve session id from environment\n"); 
            is_ready = FALSE;
        }

        if (host_pid == UINT32_MAX) {
            g_debug("Failed to resolve host pid from environment\n"); 
            is_ready = FALSE;
        }

        if (is_ready) {
            ctx = calloc(1, sizeof(AgentContext));
            if (ctx) {
                ctx->host_pid = host_pid;
                ctx->session_id = session_id;
                g_debug("Agent context initialized with host pid: %u, session id: %u\n", host_pid, session_id);
            } else {
                g_debug("Failed to allocate agent context\n");
            }
        } else {
            g_debug("Failed to initialize agent context: session id or host pid not provided nor can be resolved from environment\n");
            ctx = NULL;
        }

        g_once_init_leave(&ctx_initialized, (gsize) ctx);
    }
    return ctx;
}

// Entry point called when agent is injected (Frida's standard)
// Must be exported for Frida to find it
__attribute__((visibility("default")))
void agent_init(const gchar* data, gint data_size) {
    // Initialize GUM first before using any GLib functions
    gum_init_embedded();
    
    g_print("[Agent] agent_init called with data_size=%d\n", data_size);
    
    uint32_t arg_host = 0, arg_sid = 0;
    parse_init_payload(data, data_size, &arg_host, &arg_sid);
    g_host_pid = arg_host;
    g_session_id = arg_sid;
    
    g_print("[Agent] Parsed host_pid=%u, session_id=%08x\n", g_host_pid, g_session_id);

    AgentContext* ctx = get_shared_agent_context();

    if (ctx == NULL) {
        g_print("[Agent] Failed to allocate agent context\n");
        return;
    }
    
    g_print("[Agent] Got context, host_pid=%u, session_id=%08x\n", ctx->host_pid, ctx->session_id);
    
    // Create TLS key
    pthread_key_create(&g_tls_key, free);
    
    // Open shared memory segments using unique naming
    g_print("[Agent] Opening shared memory segments...\n");
    ctx->shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, ctx->host_pid, ctx->session_id, 4096);
    ctx->shm_index = shared_memory_open_unique(ADA_ROLE_INDEX, ctx->host_pid, ctx->session_id, 32 * 1024 * 1024);
    ctx->shm_detail = shared_memory_open_unique(ADA_ROLE_DETAIL, ctx->host_pid, ctx->session_id, 32 * 1024 * 1024);
    
    if (!ctx->shm_control || !ctx->shm_index || !ctx->shm_detail) {
        g_print("[Agent] Failed to open shared memory (control=%p, index=%p, detail=%p)\n",
                ctx->shm_control, ctx->shm_index, ctx->shm_detail);
        return;
    }
    
    g_print("[Agent] Successfully opened all shared memory segments\n");
    
    // Map control block
    ctx->control_block = (ControlBlock*)shared_memory_get_address(ctx->shm_control);
    g_print("[Agent] Control block mapped at %p\n", ctx->control_block);
    
    // Attach to existing ring buffers (already initialized by controller)
    void* index_addr = shared_memory_get_address(ctx->shm_index);
    void* detail_addr = shared_memory_get_address(ctx->shm_detail);
    g_print("[Agent] Ring buffer addresses: index=%p, detail=%p\n", index_addr, detail_addr);
    
    ctx->index_ring = ring_buffer_attach(index_addr, 32 * 1024 * 1024, sizeof(IndexEvent));
    g_print("[Agent] Index ring attached: %p\n", ctx->index_ring);
    
    ctx->detail_ring = ring_buffer_attach(detail_addr, 32 * 1024 * 1024, sizeof(DetailEvent));
    g_print("[Agent] Detail ring attached: %p\n", ctx->detail_ring);
    
    // Get interceptor
    ctx->interceptor = gum_interceptor_obtain();
    g_print("[Agent] Got interceptor: %p\n", ctx->interceptor);
    
    // Begin transaction for batch hooking
    gum_interceptor_begin_transaction(ctx->interceptor);
    g_print("[Agent] Beginning hook installation...\n");
    
    // Hook some specific functions for POC
    // Note: Full enumeration would require different API approach
    const char* functions_to_hook[] = {
        "open", "close", "read", "write",
        "malloc", "free", "memcpy", "memset",
        NULL
    };
    
    guint function_id = 0;
    for (const char** fname = functions_to_hook; *fname != NULL; fname++) {
        GumAddress func_addr = gum_module_find_export_by_name(NULL, *fname);
        if (func_addr != 0) {
            HookData* hook = g_new0(HookData, 1);
            hook->ctx = ctx;
            hook->function_id = function_id++;
            hook->function_name = g_strdup(*fname);
            
            GumInvocationListener* listener = gum_make_call_listener(
                on_enter, on_leave, hook, g_free);
            
            gum_interceptor_attach(ctx->interceptor,
                                  GSIZE_TO_POINTER(func_addr),
                                  listener,
                                  hook,
                                  GUM_ATTACH_FLAGS_NONE);
            
            ctx->num_hooks++;
            g_print("[Agent] Hooked: %s at 0x%llx\n", *fname, func_addr);
        }
    }
    
    // End transaction
    gum_interceptor_end_transaction(ctx->interceptor);
    
    g_print("[Agent] Installed %u hooks\n", ctx->num_hooks);
    g_print("[Agent] Initialization complete\n");
}

// Called when agent is being unloaded
G_GNUC_INTERNAL void agent_deinit(void) {
    AgentContext* ctx = get_shared_agent_context();

    if (!ctx) return;
    
    // Detach all hooks
    if (ctx->interceptor) {
        // Note: In production, we'd need to track listeners and detach individually
        // For POC, just unref the interceptor
        g_object_unref(ctx->interceptor);
    }
    
    // Cleanup ring buffers
    ring_buffer_destroy(ctx->index_ring);
    ring_buffer_destroy(ctx->detail_ring);
    
    // Cleanup shared memory
    shared_memory_destroy(ctx->shm_control);
    shared_memory_destroy(ctx->shm_index);
    shared_memory_destroy(ctx->shm_detail);
    
    free(ctx);
    ctx = NULL;
    
    pthread_key_delete(g_tls_key);
    
    gum_deinit_embedded();
}