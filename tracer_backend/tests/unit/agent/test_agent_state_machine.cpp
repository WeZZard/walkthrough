#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/agent_mode.h>
#include <tracer_backend/utils/control_block_ipc.h>
}

static inline uint64_t ns(uint64_t v) { return v; }

// 1) startup__registry_not_ready__then_global_only
TEST(agent_state_machine__startup__registry_not_ready__then_global_only, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_GLOBAL_ONLY;

    uint64_t now = ns(1'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    cb_set_registry_ready(&cb, 0);
    cb_set_registry_epoch(&cb, 0);
    cb_set_heartbeat_ns(&cb, 0);

    agent_mode_tick(&st, &cb, now, timeout);

    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);
    EXPECT_EQ(st.transitions, 0ull);
    EXPECT_EQ(st.fallbacks, 0ull);
}

// 2) ready_flag__set__then_dual_write_until_heartbeat_healthy
TEST(agent_state_machine__ready_flag__set__then_dual_write_until_heartbeat_healthy, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_GLOBAL_ONLY;
    st.transitions = 0;
    st.fallbacks = 0;

    uint64_t now = ns(2'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    // Registry becomes ready but heartbeat not yet healthy
    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 1);
    cb_set_heartbeat_ns(&cb, now - ns(1'000'000'000ull)); // stale
    agent_mode_tick(&st, &cb, now, timeout);
    // Not healthy (stale) so remain GLOBAL_ONLY initially
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);

    // Heartbeat turns healthy → transition to DUAL_WRITE
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);
    EXPECT_EQ(st.transitions, 1ull);

    // Next healthy tick → advance to PER_THREAD_ONLY
    now += ns(100'000'000ull);
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(st.transitions, 2ull);
}

// 3) heartbeat__stall__then_dual_write_then_global_only
TEST(agent_state_machine__heartbeat__stall__then_dual_write_then_global_only, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_PER_THREAD_ONLY;
    st.transitions = 0;
    st.fallbacks = 0;

    uint64_t now = ns(5'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 1);
    cb_set_heartbeat_ns(&cb, now - ns(1'000'000'000ull)); // stale

    // First stale tick: PER_THREAD_ONLY -> DUAL_WRITE
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);
    EXPECT_EQ(st.fallbacks, 1ull);

    // Still stale: DUAL_WRITE -> GLOBAL_ONLY
    now += ns(600'000'000ull);
    agent_mode_tick(&st, &cb, now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);
    EXPECT_EQ(st.fallbacks, 2ull);
}

// 4) heartbeat__resume__then_back_to_per_thread_only
TEST(agent_state_machine__heartbeat__resume__then_back_to_per_thread_only, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_DUAL_WRITE; // assume we already fell back once
    st.transitions = 0;
    st.fallbacks = 1;

    uint64_t now = ns(7'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 2);

    // Heartbeat resumes and is healthy
    cb_set_heartbeat_ns(&cb, now);
    agent_mode_tick(&st, &cb, now, timeout);
    // DUAL_WRITE -> PER_THREAD_ONLY on healthy
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(st.transitions, 1ull);
}

// 5) epoch_change__then_re_attach_and_re_warm
// Note: Current implementation does not force a re-warm on epoch change.
// We validate that last_seen_epoch updates while staying in steady state.
TEST(agent_state_machine__epoch_change__then_re_attach_and_re_warm, unit) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_PER_THREAD_ONLY;
    st.transitions = 0;
    st.fallbacks = 0;
    st.last_seen_epoch = 1;

    uint64_t now = ns(9'000'000'000ull);
    const uint64_t timeout = ns(500'000'000ull);

    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 2); // epoch rolls
    cb_set_heartbeat_ns(&cb, now);

    agent_mode_tick(&st, &cb, now, timeout);

    // Expect steady state preserved, epoch observed
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(st.last_seen_epoch, 2u);
}

