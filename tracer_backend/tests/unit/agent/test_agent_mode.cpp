#include <gtest/gtest.h>
#include <tracer_backend/utils/agent_mode.h>
#include <tracer_backend/utils/control_block_ipc.h>

static uint64_t ns(uint64_t v) { return v; }

TEST(agent_mode__transitions__then_progresses_and_fallbacks, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_GLOBAL_ONLY;
    st.transitions = 0;
    st.fallbacks = 0;

    uint64_t now = ns(1'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    // Initially not ready -> remain global-only
    cb_set_registry_ready(&cb, 0);
    cb_set_registry_epoch(&cb, 0);
    cb_set_heartbeat_ns(&cb, 0);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);
    EXPECT_EQ(st.transitions, 0ull);
    EXPECT_EQ(st.fallbacks, 0ull);

    // Make registry healthy -> advance to DUAL_WRITE
    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 1);
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);
    EXPECT_EQ(st.transitions, 1ull);

    // Healthy again -> advance to PER_THREAD_ONLY
    now += ns(100'000'000ull);
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(st.transitions, 2ull);

    // Heartbeat goes stale -> fallback to DUAL_WRITE
    now += ns(1'000'000'000ull); // stale by > timeout
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);
    EXPECT_EQ(st.fallbacks, 1ull);

    // Still stale -> fallback to GLOBAL_ONLY
    now += ns(1'000'000'000ull);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);
    EXPECT_EQ(st.fallbacks, 2ull);
}

TEST(controller__heartbeat__then_updates_monotonic, unit) {
    ControlBlock cb = {};
    // Heartbeat starts at 0
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), 0ull);
    // Update increases
    uint64_t t1 = ns(123456789ull);
    uint64_t t2 = ns(223456789ull);
    cb_set_heartbeat_ns(&cb, t1);
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), t1);
    cb_set_heartbeat_ns(&cb, t2);
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), t2);
}

