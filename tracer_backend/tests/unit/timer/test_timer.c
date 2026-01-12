#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <thread>

extern "C" {
#include <tracer_backend/timer/timer.h>
}

#include "timer_test_support.h"

static std::atomic<int> g_shutdown_calls{0};

namespace {
constexpr int kTimerWhiteboxClockQueueMax = 32;
}

extern "C" void shutdown_initiate(void) {
    g_shutdown_calls.fetch_add(1, std::memory_order_relaxed);
}

class TimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer_test_control_reset();
        ASSERT_EQ(0, timer_init());
        g_shutdown_calls.store(0, std::memory_order_relaxed);
    }

    void TearDown() override {
        timer_cleanup();
        timer_test_control_reset();
    }
};

class TimerWhiteboxTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer_test_control_reset();
    }

    void TearDown() override {
        timer_test_control_reset();
    }
};

TEST_F(TimerTest, timer__start_and_cancel__then_inactive) {
    ASSERT_EQ(0, timer_start(300));
    EXPECT_TRUE(timer_is_active());
    EXPECT_GT(timer_remaining_ms(), 0u);

    EXPECT_EQ(0, timer_cancel());
    for (int attempt = 0; attempt < 20 && timer_is_active(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_FALSE(timer_is_active());
}

TEST_F(TimerTest, timer__remaining_ms_decreases__then_reports_progress) {
    ASSERT_EQ(0, timer_start(500));
    uint64_t initial_remaining = timer_remaining_ms();
    EXPECT_GT(initial_remaining, 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    uint64_t later_remaining = timer_remaining_ms();
    EXPECT_GT(initial_remaining, later_remaining);

    EXPECT_EQ(0, timer_cancel());

    for (int attempt = 0; attempt < 20 && timer_is_active(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_FALSE(timer_is_active());
}

TEST_F(TimerTest, timer__expires__then_shutdown_invoked) {
    ASSERT_EQ(0, timer_start(150));

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    EXPECT_FALSE(timer_is_active());
    EXPECT_EQ(1, g_shutdown_calls.load(std::memory_order_relaxed));
}

TEST_F(TimerTest, timer__init_when_already_initialized__then_returns_success) {
    EXPECT_EQ(0, timer_init());
}

TEST_F(TimerTest, timer__start_when_not_initialized__then_sets_einval) {
    timer_cleanup();
    errno = 0;
    EXPECT_EQ(-1, timer_start(100));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(TimerTest, timer__start_with_zero_duration__then_sets_einval) {
    errno = 0;
    EXPECT_EQ(-1, timer_start(0));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(TimerTest, timer__start_when_already_active__then_sets_ebusy) {
    ASSERT_EQ(0, timer_start(200));
    errno = 0;
    EXPECT_EQ(-1, timer_start(100));
    EXPECT_EQ(EBUSY, errno);

    EXPECT_EQ(0, timer_cancel());
    for (int attempt = 0; attempt < 30 && timer_is_active(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(timer_is_active());
}

TEST_F(TimerTest, timer__cancel_when_uninitialized__then_returns_success) {
    timer_cleanup();
    EXPECT_EQ(0, timer_cancel());
}

TEST_F(TimerTest, timer__remaining_ms_when_uninitialized__then_zero) {
    timer_cleanup();
    EXPECT_EQ(0u, timer_remaining_ms());
}

TEST_F(TimerTest, timer__remaining_ms_when_inactive__then_zero) {
    EXPECT_EQ(0u, timer_remaining_ms());
}

TEST_F(TimerTest, timer__remaining_ms_when_elapsed__then_zero) {
    ASSERT_EQ(0, timer_start(60));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(0u, timer_remaining_ms());
}

TEST_F(TimerTest, timer__is_active_when_uninitialized__then_false) {
    timer_cleanup();
    EXPECT_FALSE(timer_is_active());
}

TEST_F(TimerTest, timer__cleanup_when_uninitialized__then_noop) {
    timer_cleanup();
    EXPECT_NO_FATAL_FAILURE(timer_cleanup());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__start_when_clock_fails__then_returns_error) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_fail_clock_gettime(1, EIO);

    errno = 0;
    EXPECT_EQ(-1, timer_start_whitebox(25));
    EXPECT_EQ(EFAULT, errno);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__start_when_thread_creation_fails__then_propagates_error) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_fail_pthread_create(1, EAGAIN);

    errno = 0;
    EXPECT_EQ(-1, timer_start_whitebox(50));
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__decrement_retry__then_records_retry) {
    constexpr int kThreadCount = 4;
    int observed_retries = 0;

    for (int attempt = 0; attempt < 10 && observed_retries == 0; ++attempt) {
        timer_test_control_reset_decrement_metrics();
        timer_test_control_fail_clock_gettime(kThreadCount, EIO);
        timer_test_control_force_decrement_retry();

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::thread threads[kThreadCount];

        for (int i = 0; i < kThreadCount; ++i) {
            threads[i] = std::thread([&]() {
                ready.fetch_add(1, std::memory_order_acq_rel);
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                timer_test_control_consume_clock_failure();
            });
        }

        while (ready.load(std::memory_order_acquire) < kThreadCount) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        for (int i = 0; i < kThreadCount; ++i) {
            threads[i].join();
        }

        observed_retries = timer_test_control_get_decrement_retry_count();
    }

    timer_test_control_fail_clock_gettime(0, 0);
    timer_test_control_reset_decrement_metrics();

    EXPECT_GT(observed_retries, 0);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__init_when_uninitialized__then_resets_fields) {
    timer_test_manager_state_t state = {
        .initialized = false,
        .active = true,
        .stop_requested = true,
        .thread_joinable = true,
        .duration_ms = 42,
        .start_ns = 1000,
    };
    timer_test_control_set_manager_state(state);

    EXPECT_EQ(0, timer_init_whitebox());

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_TRUE(after.initialized);
    EXPECT_FALSE(after.active);
    EXPECT_FALSE(after.stop_requested);
    EXPECT_FALSE(after.thread_joinable);
    EXPECT_EQ(0u, after.duration_ms);
    EXPECT_EQ(0u, after.start_ns);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timer_join_if_needed_when_not_joinable__then_noop) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_reset_pthread();

    timer_test_timer_join_if_needed();
    EXPECT_EQ(0, timer_test_control_get_pthread_join_count());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timer_join_if_needed_when_joinable__then_clears_flag) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = true,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_reset_pthread();

    timer_test_timer_join_if_needed();

    EXPECT_EQ(1, timer_test_control_get_pthread_join_count());
    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_FALSE(after.thread_joinable);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__cleanup_when_initialized__then_resets_fields) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = true,
        .thread_joinable = true,
        .duration_ms = 88,
        .start_ns = 1234,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_reset_pthread();

    timer_cleanup_whitebox();

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_FALSE(after.initialized);
    EXPECT_FALSE(after.active);
    EXPECT_FALSE(after.stop_requested);
    EXPECT_FALSE(after.thread_joinable);
    EXPECT_EQ(0u, after.duration_ms);
    EXPECT_EQ(0u, after.start_ns);
    EXPECT_EQ(1, timer_test_control_get_pthread_join_count());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__remaining_ms_with_invalid_state__then_returns_zero) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    EXPECT_EQ(0u, timer_remaining_ms_whitebox());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__remaining_ms_when_elapsed_exceeds_duration__then_returns_zero) {
    timer_test_control_reset_clock_queue();
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 3,
        .start_ns = 1000000ULL,
    };
    timer_test_control_set_manager_state(state);

    struct timespec now = {
        .tv_sec = 0,
        .tv_nsec = 6000000L,
    };
    timer_test_control_enqueue_clock_time(now);

    EXPECT_EQ(0u, timer_remaining_ms_whitebox());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timespec_null__then_returns_zero) {
    EXPECT_EQ(0u, timer_test_timespec_to_ns(NULL));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timespec_negative_seconds__then_returns_zero) {
    struct timespec ts = {.tv_sec = -1, .tv_nsec = 0};
    EXPECT_EQ(0u, timer_test_timespec_to_ns(&ts));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timespec_seconds_overflow__then_returns_uint64_max) {
    struct timespec ts = {
        .tv_sec = (time_t)(UINT64_MAX / 1000000000ULL),
        .tv_nsec = 0,
    };
    EXPECT_EQ(UINT64_MAX, timer_test_timespec_to_ns(&ts));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timespec_addition_overflow__then_returns_uint64_max) {
    struct timespec ts = {
        .tv_sec = (time_t)((UINT64_MAX / 1000000000ULL) - 1ULL),
        .tv_nsec = (long)UINT64_MAX,
    };
    EXPECT_EQ(UINT64_MAX, timer_test_timespec_to_ns(&ts));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__timespec_valid__then_combines_components) {
    struct timespec ts = {
        .tv_sec = 2,
        .tv_nsec = 5000000L,
    };
    EXPECT_EQ(2000000000ULL + 5000000ULL, timer_test_timespec_to_ns(&ts));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__current_time_failure__then_returns_zero) {
    timer_test_control_fail_clock_gettime(1, EFAULT);
    EXPECT_EQ(0u, timer_test_current_time_ns());
}

TEST_F(TimerWhiteboxTest, timer_whitebox__calculate_elapsed_zero_start__then_returns_zero) {
    EXPECT_EQ(0u, calculate_elapsed_ms_whitebox(0));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__calculate_elapsed_future_start__then_returns_zero) {
    EXPECT_EQ(0u, calculate_elapsed_ms_whitebox(UINT64_MAX));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__calculate_elapsed_progress__then_returns_delta) {
    timer_test_control_reset_clock_queue();
    struct timespec now = {
        .tv_sec = 0,
        .tv_nsec = 7500000L,
    };
    timer_test_control_enqueue_clock_time(now);

    uint64_t start_ns = 2500000ULL;
    EXPECT_EQ(5u, calculate_elapsed_ms_whitebox(start_ns));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__start_success__then_sets_thread_joinable) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_reset_clock_queue();

    struct timespec start_ts = {.tv_sec = 0, .tv_nsec = 1000000L};
    struct timespec later_ts = {.tv_sec = 0, .tv_nsec = 7000000L};
    timer_test_control_enqueue_clock_time(start_ts);
    timer_test_control_enqueue_clock_time(later_ts);

    timer_test_control_set_pthread_inline(true);
    timer_test_control_fail_nanosleep(1, EIO, false);

    EXPECT_EQ(0, timer_start_whitebox(25));

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_TRUE(after.active);
    EXPECT_TRUE(after.thread_joinable);

    timer_test_timer_join_if_needed();
    timer_test_manager_state_t joined = timer_test_control_get_manager_state();
    EXPECT_FALSE(joined.thread_joinable);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__start_without_inline__then_thread_routine_deferred) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_reset_pthread();
    timer_test_control_set_pthread_inline(false);
    timer_test_control_reset_clock_queue();

    struct timespec start_ts = {.tv_sec = 0, .tv_nsec = 1000000L};
    struct timespec later_ts = {.tv_sec = 0, .tv_nsec = 7000000L};
    timer_test_control_enqueue_clock_time(start_ts);
    timer_test_control_enqueue_clock_time(later_ts);

    g_shutdown_calls.store(0, std::memory_order_relaxed);

    EXPECT_EQ(0, timer_start_whitebox(10));

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_TRUE(after.active);
    EXPECT_TRUE(after.thread_joinable);
    EXPECT_EQ(0, g_shutdown_calls.load(std::memory_order_relaxed));

    timer_test_timer_join_if_needed();
}

TEST_F(TimerWhiteboxTest, timer_whitebox__clock_queue_overflow__then_discards_additional_values) {
    timer_test_control_reset_clock_queue();

    for (int i = 0; i < kTimerWhiteboxClockQueueMax + 8; ++i) {
        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = (long)i,
        };
        timer_test_control_enqueue_clock_time(ts);
    }

    for (int i = 0; i < kTimerWhiteboxClockQueueMax; ++i) {
        EXPECT_EQ(static_cast<uint64_t>(i), timer_test_current_time_ns());
    }
}

TEST_F(TimerWhiteboxTest, interruptible_sleep_whitebox__zero_duration__then_returns_true) {
    EXPECT_TRUE(interruptible_sleep_ms_whitebox(0));
}

TEST_F(TimerWhiteboxTest, interruptible_sleep_whitebox__nanosleep_non_eintr__then_returns_false) {
    timer_test_control_fail_nanosleep(1, EIO, false);
    EXPECT_FALSE(interruptible_sleep_ms_whitebox(5));
}

TEST_F(TimerWhiteboxTest, interruptible_sleep_whitebox__nanosleep_eintr_with_stop__then_returns_false) {
    timer_test_control_fail_nanosleep(1, EINTR, true);
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = true,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    EXPECT_FALSE(interruptible_sleep_ms_whitebox(5));
}

TEST_F(TimerWhiteboxTest, interruptible_sleep_whitebox__nanosleep_eintr_then_succeeds__then_returns_true) {
    timer_test_control_fail_nanosleep(1, EINTR, true);
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    EXPECT_TRUE(interruptible_sleep_ms_whitebox(5));
}

TEST_F(TimerWhiteboxTest, timer_whitebox__thread_stop_requested_immediately__then_clears_flags) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = true,
        .thread_joinable = false,
        .duration_ms = 100,
        .start_ns = 123,
    };
    timer_test_control_set_manager_state(state);
    timer_thread_func_whitebox(NULL);

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_FALSE(after.active);
    EXPECT_FALSE(after.stop_requested);
}

TEST_F(TimerWhiteboxTest, timer_whitebox__thread_sleep_failure__then_clears_flags) {
    timer_test_manager_state_t state = {
        .initialized = true,
        .active = true,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 100,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(state);
    timer_test_control_fail_nanosleep(1, EIO, false);

    timer_thread_func_whitebox(NULL);

    timer_test_manager_state_t after = timer_test_control_get_manager_state();
    EXPECT_FALSE(after.active);
    EXPECT_FALSE(after.stop_requested);
}
