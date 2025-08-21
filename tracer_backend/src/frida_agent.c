#include <frida-gum.h>
#include "tracer_types.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mach/mach_time.h>

#ifdef __APPLE__
#include <pthread/pthread.h>
#include <mach/thread_info.h>
#include <mach/mach_init.h>
#endif

typedef struct {
    GumInterceptor* interceptor;
    SharedMemory* shm_index;
    SharedMemory* shm_detail;
    SharedMemory* shm_control;
    RingBuffer* index_ring;
    RingBuffer* detail_ring;
    ControlBlock* control_block;
    guint num_hooks;
    guint64 module_base;
} AgentContext;

typedef struct {
    AgentContext* ctx;
    guint64 function_id;
    const char* function_name;
} HookData;

static AgentContext* g_agent_ctx = NULL;
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
G_GNUC_INTERNAL void agent_init(const gchar* data, gint data_size);

// Exported entry point for manual initialization
__attribute__((visibility("default")))
void frida_agent_main(void) {
    agent_init(NULL, 0);
}

// Entry point called when agent is injected (Frida's standard)
G_GNUC_INTERNAL void agent_init(const gchar* data, gint data_size) {
    gum_init_embedded();
    
    // Create TLS key
    pthread_key_create(&g_tls_key, free);
    
    // Create agent context
    g_agent_ctx = calloc(1, sizeof(AgentContext));
    
    // Open shared memory segments
    g_agent_ctx->shm_control = shared_memory_open("ada_control", 4096);
    g_agent_ctx->shm_index = shared_memory_open("ada_index", 32 * 1024 * 1024);
    g_agent_ctx->shm_detail = shared_memory_open("ada_detail", 32 * 1024 * 1024);
    
    if (!g_agent_ctx->shm_control || !g_agent_ctx->shm_index || !g_agent_ctx->shm_detail) {
        g_printerr("Failed to open shared memory\n");
        return;
    }
    
    // Map control block
    g_agent_ctx->control_block = (ControlBlock*)g_agent_ctx->shm_control->address;
    
    // Attach to existing ring buffers (already initialized by controller)
    g_agent_ctx->index_ring = ring_buffer_attach(g_agent_ctx->shm_index->address,
                                                 32 * 1024 * 1024,
                                                 sizeof(IndexEvent));
    g_agent_ctx->detail_ring = ring_buffer_attach(g_agent_ctx->shm_detail->address,
                                                  32 * 1024 * 1024,
                                                  sizeof(DetailEvent));
    
    // Get interceptor
    g_agent_ctx->interceptor = gum_interceptor_obtain();
    
    // Begin transaction for batch hooking
    gum_interceptor_begin_transaction(g_agent_ctx->interceptor);
    
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
            hook->ctx = g_agent_ctx;
            hook->function_id = function_id++;
            hook->function_name = g_strdup(*fname);
            
            GumInvocationListener* listener = gum_make_call_listener(
                on_enter, on_leave, hook, g_free);
            
            gum_interceptor_attach(g_agent_ctx->interceptor,
                                  GSIZE_TO_POINTER(func_addr),
                                  listener,
                                  hook,
                                  GUM_ATTACH_FLAGS_NONE);
            
            g_agent_ctx->num_hooks++;
            g_print("[Agent] Hooked: %s at 0x%llx\n", *fname, func_addr);
        }
    }
    
    // End transaction
    gum_interceptor_end_transaction(g_agent_ctx->interceptor);
    
    g_print("[Agent] Installed %u hooks\n", g_agent_ctx->num_hooks);
}

// Called when agent is being unloaded
G_GNUC_INTERNAL void agent_deinit(void) {
    if (!g_agent_ctx) return;
    
    // Detach all hooks
    if (g_agent_ctx->interceptor) {
        // Note: In production, we'd need to track listeners and detach individually
        // For POC, just unref the interceptor
        g_object_unref(g_agent_ctx->interceptor);
    }
    
    // Cleanup ring buffers
    ring_buffer_destroy(g_agent_ctx->index_ring);
    ring_buffer_destroy(g_agent_ctx->detail_ring);
    
    // Cleanup shared memory
    shared_memory_destroy(g_agent_ctx->shm_control);
    shared_memory_destroy(g_agent_ctx->shm_index);
    shared_memory_destroy(g_agent_ctx->shm_detail);
    
    free(g_agent_ctx);
    g_agent_ctx = NULL;
    
    pthread_key_delete(g_tls_key);
    
    gum_deinit_embedded();
}