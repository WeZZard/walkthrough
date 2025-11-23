// Unit tests for startup timeout configuration and estimation (M1_E6_I1)

#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/control_block_ipc.h>
}

#include "../../../src/controller/frida_controller_internal.h"

using ada::internal::StartupTimeoutConfig;

TEST(startup_timeout__compute_from_symbol_count__then_uses_tolerance, behavior) {
    StartupTimeoutConfig cfg;
    cfg.startup_ms = 1000;
    cfg.per_symbol_ms = 10;
    cfg.tolerance_pct = 0.5;  // 50%
    cfg.override_ms = 0;

    // estimated_ms = 1000 + 5 * 10 = 1050
    // timeout_ms   = 1050 * 1.5 = 1575
    uint32_t timeout = cfg.compute_timeout_ms(5);
    EXPECT_EQ(timeout, 1575u);
}

TEST(startup_timeout__env_override_timeout__then_bypasses_estimation, behavior) {
    // Ensure override env var is set
    setenv("ADA_STARTUP_TIMEOUT", "90000", 1);
    unsetenv("ADA_STARTUP_WARM_UP_DURATION");
    unsetenv("ADA_STARTUP_PER_SYMBOL_COST");
    unsetenv("ADA_STARTUP_TIMEOUT_TOLERANCE");

    StartupTimeoutConfig cfg = StartupTimeoutConfig::from_env();
    EXPECT_EQ(cfg.override_ms, 90000u);

    // Symbol count should be ignored when override_ms > 0
    uint32_t timeout = cfg.compute_timeout_ms(12345);
    EXPECT_EQ(timeout, 90000u);

    unsetenv("ADA_STARTUP_TIMEOUT");
}

TEST(startup_timeout__env_calibration_params__then_estimation_uses_values, behavior) {
    setenv("ADA_STARTUP_WARM_UP_DURATION", "2000", 1);
    setenv("ADA_STARTUP_PER_SYMBOL_COST", "5", 1);
    setenv("ADA_STARTUP_TIMEOUT_TOLERANCE", "0.25", 1);
    unsetenv("ADA_STARTUP_TIMEOUT");

    StartupTimeoutConfig cfg = StartupTimeoutConfig::from_env();
    EXPECT_EQ(cfg.startup_ms, 2000u);
    EXPECT_EQ(cfg.per_symbol_ms, 5u);
    EXPECT_NEAR(cfg.tolerance_pct, 0.25, 1e-6);
    EXPECT_EQ(cfg.override_ms, 0u);

    // estimated_ms = 2000 + 4 * 5 = 2020
    // timeout_ms   = 2020 * 1.25 = 2525
    uint32_t timeout = cfg.compute_timeout_ms(4);
    EXPECT_EQ(timeout, 2525u);

    unsetenv("ADA_STARTUP_WARM_UP_DURATION");
    unsetenv("ADA_STARTUP_PER_SYMBOL_COST");
    unsetenv("ADA_STARTUP_TIMEOUT_TOLERANCE");
}

