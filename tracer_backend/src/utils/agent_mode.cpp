// Agent mode state machine implementation
#include <tracer_backend/utils/agent_mode.h>
#include <tracer_backend/utils/tracer_types.h>

extern "C" void agent_mode_tick(AgentModeState* state, const ControlBlock* cb,
                                 uint64_t now_ns, uint64_t hb_timeout_ns) {
    if (!state || !cb) return;
    // Load IPC fields with acquire semantics
    uint32_t ready = __atomic_load_n(&cb->registry_ready, __ATOMIC_ACQUIRE);
    uint32_t epoch = __atomic_load_n(&cb->registry_epoch, __ATOMIC_ACQUIRE);
    uint64_t hb = __atomic_load_n(&cb->drain_heartbeat_ns, __ATOMIC_ACQUIRE);

    bool healthy = (ready != 0) && (epoch > 0) && (hb != 0) && (now_ns >= hb) && ((now_ns - hb) <= hb_timeout_ns);

    if (healthy) {
        if (state->mode == REGISTRY_MODE_GLOBAL_ONLY) {
            state->mode = REGISTRY_MODE_DUAL_WRITE;
            state->transitions++;
            state->last_seen_epoch = epoch;
        } else if (state->mode == REGISTRY_MODE_DUAL_WRITE) {
            state->mode = REGISTRY_MODE_PER_THREAD_ONLY;
            state->transitions++;
            state->last_seen_epoch = epoch;
        } else {
            // Already per-thread-only; remain
            state->last_seen_epoch = epoch;
        }
    } else {
        if (state->mode == REGISTRY_MODE_PER_THREAD_ONLY) {
            state->mode = REGISTRY_MODE_DUAL_WRITE;
            state->fallbacks++;
        } else if (state->mode == REGISTRY_MODE_DUAL_WRITE) {
            state->mode = REGISTRY_MODE_GLOBAL_ONLY;
            state->fallbacks++;
        } else {
            // Already global-only; remain
        }
    }
}

