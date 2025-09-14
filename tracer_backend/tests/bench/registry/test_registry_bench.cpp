#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/agent_mode.h>
#include <tracer_backend/utils/control_block_ipc.h>
}

#include <chrono>
#include <thread>

using clock_mono = std::chrono::steady_clock;

static inline uint64_t to_ns(clock_mono::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

// warmup_window__under_target: verify that with healthy heartbeat the
// state machine advances to PER_THREAD_ONLY within 2 seconds.
TEST(registry_bench__warmup_window__under_target, bench) {
    ControlBlock cb = {};
    AgentModeState st = {};
    st.mode = REGISTRY_MODE_GLOBAL_ONLY;

    const uint64_t timeout_ns = 500'000'000ull; // 500ms
    cb_set_registry_ready(&cb, 1);
    cb_set_registry_epoch(&cb, 1);

    auto start = clock_mono::now();
    auto now = start;
    // Heartbeat healthy from the beginning
    cb_set_heartbeat_ns(&cb, to_ns(now));

    // First healthy tick should move to DUAL_WRITE, second to PER_THREAD_ONLY
    for (int i = 0; i < 10 && st.mode != REGISTRY_MODE_PER_THREAD_ONLY; i++) {
        agent_mode_tick(&st, &cb, to_ns(now), timeout_ns);
        cb_set_heartbeat_ns(&cb, to_ns(now));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        now = clock_mono::now();
    }

    auto end = clock_mono::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_EQ(st.mode, (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_LT(elapsed_ms, 2000) << "Warm-up exceeded 2 seconds";
}

// steady_state__throughput_equal_baseline: in steady-state (per-thread-only),
// the mode checks should not impose measurable penalty vs. a baseline loop.
// We approximate by comparing two tight loops with and without an atomic read.
TEST(registry_bench__steady_state__throughput_equal_baseline, bench) {
    constexpr size_t N = 5'000'000; // 5M iterations

    volatile uint64_t sink = 0;

    // Baseline loop (no mode checks)
    auto t0 = clock_mono::now();
    for (size_t i = 0; i < N; i++) {
        sink += (i & 1);
    }
    auto t1 = clock_mono::now();

    // Steady-state loop with mode check (simulated PER_THREAD_ONLY)
    ControlBlock cb = {};
    cb_set_registry_mode(&cb, REGISTRY_MODE_PER_THREAD_ONLY);
    auto t2 = clock_mono::now();
    for (size_t i = 0; i < N; i++) {
        // Load and branch on mode (similar to capture path)
        uint32_t mode = cb_get_registry_mode(&cb);
        if (mode == REGISTRY_MODE_PER_THREAD_ONLY) {
            sink += (i & 1);
        } else {
            sink += (i & 1);
        }
    }
    auto t3 = clock_mono::now();

    auto base_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto steady_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    // Allow modest noise; require within 100% overhead or 10ms absolute slack.
    // Debug builds can be significantly slower than release builds.
    long long allowed = std::max((long long)((base_ms + 1) * 2.0), base_ms + 10);
    EXPECT_LE(steady_ms, allowed)
        << "Steady-state mode checks impose unexpected penalty (" << base_ms << "ms -> " << steady_ms << "ms)";

    (void)sink; // use sink to prevent optimizing away
}
