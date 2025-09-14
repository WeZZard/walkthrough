#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/agent_mode.h>
#include <tracer_backend/utils/control_block_ipc.h>
}

static inline uint64_t ns(uint64_t v) { return v; }

// Validate that repeated ticks with healthy heartbeat progress through
// GLOBAL_ONLY -> DUAL_WRITE -> PER_THREAD_ONLY prior to event capture.
TEST(agent_mode_tick__healthy_heartbeat__then_reaches_per_thread_only, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_GLOBAL_ONLY;

    const uint64_t timeout = ns(500'000'000ull);
    uint64_t now = ns(1'000'000'000ull);

    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 1);

    // First stale heartbeat: remain GLOBAL_ONLY
    cb_set_heartbeat_ns(&cb, now - ns(1'000'000'000ull));
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);

    // Healthy heartbeat: advance to DUAL_WRITE
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);

    // Next healthy tick: advance to PER_THREAD_ONLY
    now += ns(100'000'000ull);
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
}

