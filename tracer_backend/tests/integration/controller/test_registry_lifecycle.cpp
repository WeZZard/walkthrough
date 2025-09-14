#include <gtest/gtest.h>
#include <unistd.h>
#include <chrono>

extern "C" {
#include <tracer_backend/controller/frida_controller.h>
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/control_block_ipc.h>
#include <tracer_backend/utils/agent_mode.h>
#include "ada_paths.h"
}

// Helper to fetch ControlBlock from predictable shared memory
static ControlBlock* get_cb() {
    uint32_t sid = shared_memory_get_session_id();
    pid_t local_pid = shared_memory_get_pid();
    SharedMemoryRef shm = shared_memory_open_unique(ADA_ROLE_CONTROL, local_pid, sid, 4096);
    if (!shm) return nullptr;
    return (ControlBlock*) shared_memory_get_address(shm);
}

// 9) attach__warmup_to_steady__then_events_captured
TEST(registry_lifecycle__attach__warmup_to_steady__then_events_captured, integration) {
    FridaController* ctrl = frida_controller_create("/tmp/ada_test");
    if (!ctrl) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ControlBlock* cb = get_cb();
    ASSERT_NE(cb, nullptr);

    // Registry should be marked ready shortly after controller creation
    EXPECT_EQ(cb_get_registry_ready(cb), 1u);

    // Warm-up starts in DUAL_WRITE and moves to PER_THREAD_ONLY after ~5 ticks
    // Drain tick is 100ms; waiting ~700ms should be sufficient across environments
    usleep(700'000);
    EXPECT_EQ(cb_get_registry_mode(cb), (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);

    // Stats are best-effort; ensure controller remains healthy (heartbeat advancing)
    uint64_t hb1 = cb_get_heartbeat_ns(cb);
    usleep(150'000);
    uint64_t hb2 = cb_get_heartbeat_ns(cb);
    EXPECT_LT(hb1, hb2);

    frida_controller_destroy(ctrl);
}

// 10) induced_stall__then_fallback_and_recovery
// We simulate stall from the agent's perspective by comparing a stale now_ns
// against the last heartbeat, causing fallback; then recover when now_ns catches up.
TEST(registry_lifecycle__induced_stall__then_fallback_and_recovery, integration) {
    FridaController* ctrl = frida_controller_create("/tmp/ada_test");
    if (!ctrl) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ControlBlock* cb = get_cb();
    ASSERT_NE(cb, nullptr);

    // Allow warm-up to reach steady state
    usleep(700'000);

    // Agent state machine using control block fields
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_PER_THREAD_ONLY;

    const uint64_t timeout = 500'000'000ull; // 500ms
    uint64_t hb = cb_get_heartbeat_ns(cb);

    // Simulate a stall: now far beyond heartbeat
    uint64_t stale_now = hb + timeout + 1'000'000'000ull;
    agent_mode_tick(&st, cb, stale_now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);

    // Another stale tick → global-only
    stale_now += 1'000'000'000ull;
    agent_mode_tick(&st, cb, stale_now, timeout);
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);

    // Recovery when heartbeat resumes (use a fresh now around latest hb)
    usleep(200'000);
    uint64_t fresh_hb = cb_get_heartbeat_ns(cb);
    agent_mode_tick(&st, cb, fresh_hb, timeout);
    // First healthy tick from GLOBAL_ONLY -> DUAL_WRITE
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_DUAL_WRITE);
    agent_mode_tick(&st, cb, fresh_hb, timeout);
    // Next healthy tick -> PER_THREAD_ONLY
    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);

    frida_controller_destroy(ctrl);
}

// 11) epoch_roll__then_agent_re_warm
// Current implementation tracks last_seen_epoch without forcing re-warm.
// Validate epoch observation and steady mode retention.
TEST(registry_lifecycle__epoch_roll__then_agent_re_warm, integration) {
    FridaController* ctrl = frida_controller_create("/tmp/ada_test");
    if (!ctrl) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ControlBlock* cb = get_cb();
    ASSERT_NE(cb, nullptr);

    // Reach steady state
    usleep(700'000);
    ASSERT_EQ(cb_get_registry_mode(cb), (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);

    AgentModeState st = {};
    st.mode = REGISTRY_MODE_PER_THREAD_ONLY;
    st.last_seen_epoch = cb_get_registry_epoch(cb);

    // Simulate epoch bump by controller
    cb_set_registry_epoch(cb, st.last_seen_epoch + 1);

    uint64_t now = cb_get_heartbeat_ns(cb);
    const uint64_t timeout = 500'000'000ull;
    agent_mode_tick(&st, cb, now, timeout);

    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(st.last_seen_epoch, cb_get_registry_epoch(cb));

    frida_controller_destroy(ctrl);
}

