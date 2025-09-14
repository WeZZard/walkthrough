// Agent Mode state machine (header-only API, implemented in cpp)
#ifndef AGENT_MODE_H
#define AGENT_MODE_H

#include <stdint.h>
#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t mode;              // Current mode (RegistryMode)
    uint64_t transitions;       // Number of transitions executed
    uint64_t fallbacks;         // Number of fallback steps executed
    uint32_t last_seen_epoch;   // Last observed epoch
} AgentModeState;

// Tick the agent mode state machine based on ControlBlock IPC fields.
// now_ns: current monotonic time in ns
// hb_timeout_ns: threshold to consider heartbeat stale
void agent_mode_tick(AgentModeState* state, const ControlBlock* cb,
                     uint64_t now_ns, uint64_t hb_timeout_ns);

#ifdef __cplusplus
}
#endif

#endif // AGENT_MODE_H

