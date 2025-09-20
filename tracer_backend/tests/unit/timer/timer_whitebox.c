#include "timer_test_support.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../../src/timer/timer_private.h"

#define TIMER_TEST_CLOCK_QUEUE_MAX 32

static atomic_int g_clock_fail_count = ATOMIC_VAR_INIT(0);
static atomic_int g_clock_fail_errno = ATOMIC_VAR_INIT(0);
static struct timespec g_clock_values[TIMER_TEST_CLOCK_QUEUE_MAX];
static atomic_int g_clock_value_count = ATOMIC_VAR_INIT(0);
static atomic_int g_clock_value_index = ATOMIC_VAR_INIT(0);

static atomic_int g_nanosleep_fail_count = ATOMIC_VAR_INIT(0);
static atomic_int g_nanosleep_error = ATOMIC_VAR_INIT(0);
static atomic_bool g_nanosleep_populate_remaining = ATOMIC_VAR_INIT(false);

static atomic_int g_pthread_create_fail_count = ATOMIC_VAR_INIT(0);
static atomic_int g_pthread_create_error = ATOMIC_VAR_INIT(0);
static atomic_int g_pthread_join_count = ATOMIC_VAR_INIT(0);
static atomic_bool g_pthread_create_run_inline = ATOMIC_VAR_INIT(false);
static atomic_int g_decrement_retry_count = ATOMIC_VAR_INIT(0);

static void timer_test_decrement_if_positive(atomic_int* value) {
    int current = atomic_load_explicit(value, memory_order_acquire);
    while (current > 0 && !atomic_compare_exchange_weak_explicit(
               value, &current, current - 1, memory_order_acq_rel, memory_order_acquire)) {
        // Retry until the decrement succeeds or the value changes.
        atomic_fetch_add_explicit(&g_decrement_retry_count, 1, memory_order_acq_rel);
    }
}

static int timer_test_clock_gettime(clockid_t clk_id, struct timespec* ts) {
    if (atomic_load_explicit(&g_clock_fail_count, memory_order_acquire) > 0) {
        timer_test_decrement_if_positive(&g_clock_fail_count);
        errno = atomic_load_explicit(&g_clock_fail_errno, memory_order_relaxed);
        return -1;
    }

    int index = atomic_fetch_add_explicit(&g_clock_value_index, 1, memory_order_acq_rel);
    int count = atomic_load_explicit(&g_clock_value_count, memory_order_acquire);
    if (index < count) {
        if (ts != NULL) {
            *ts = g_clock_values[index];
        }
        return 0;
    }

    return clock_gettime(clk_id, ts);
}

static int timer_test_nanosleep(const struct timespec* request, struct timespec* remaining) {
    if (atomic_load_explicit(&g_nanosleep_fail_count, memory_order_acquire) > 0) {
        timer_test_decrement_if_positive(&g_nanosleep_fail_count);
        errno = atomic_load_explicit(&g_nanosleep_error, memory_order_relaxed);
        if (atomic_load_explicit(&g_nanosleep_populate_remaining, memory_order_relaxed) &&
            request != NULL && remaining != NULL) {
            *remaining = *request;
        }
        return -1;
    }

    // Fast path: pretend sleep completed without actual delay to keep tests fast.
    (void)remaining;
    return 0;
}

static int timer_test_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start_routine)(void*), void* arg) {
    if (atomic_load_explicit(&g_pthread_create_fail_count, memory_order_acquire) > 0) {
        timer_test_decrement_if_positive(&g_pthread_create_fail_count);
        int error = atomic_load_explicit(&g_pthread_create_error, memory_order_relaxed);
        return error == 0 ? EAGAIN : error;
    }

    if (thread != NULL) {
        *thread = pthread_self();
    }

    (void)attr;

    if (atomic_load_explicit(&g_pthread_create_run_inline, memory_order_acquire) &&
        start_routine != NULL) {
        start_routine(arg);
    }

    return 0;
}

static int timer_test_pthread_join(pthread_t thread, void** value_ptr) {
    atomic_fetch_add_explicit(&g_pthread_join_count, 1, memory_order_acq_rel);
    (void)thread;
    (void)value_ptr;
    // Simulate a successful join without blocking on an actual thread.
    return 0;
}

#define timer_init timer_init_whitebox
#define timer_start timer_start_whitebox
#define timer_cancel timer_cancel_whitebox
#define timer_remaining_ms timer_remaining_ms_whitebox
#define timer_is_active timer_is_active_whitebox
#define timer_cleanup timer_cleanup_whitebox
#define interruptible_sleep_ms interruptible_sleep_ms_whitebox
#define calculate_elapsed_ms calculate_elapsed_ms_whitebox
#define timer_thread_func timer_thread_func_whitebox
#define shutdown_initiate shutdown_initiate_whitebox
#define g_timer_manager g_timer_manager_whitebox
#define NSEC_PER_SEC NSEC_PER_SEC_whitebox
#define NSEC_PER_MSEC NSEC_PER_MSEC_whitebox
#define timer_join_if_needed timer_test_timer_join_if_needed
#define current_time_ns timer_test_current_time_ns
#define timespec_to_ns timer_test_timespec_to_ns
#undef TIMER_MONITOR_INTERVAL_MS
#define TIMER_MONITOR_INTERVAL_MS 0ULL
#define pthread_join timer_test_pthread_join
#define pthread_create timer_test_pthread_create
#define nanosleep timer_test_nanosleep
#define clock_gettime timer_test_clock_gettime
#define static
#include "../../../src/timer/timer.c"
#undef static
#undef clock_gettime
#undef nanosleep
#undef pthread_create
#undef pthread_join
#undef timespec_to_ns
#undef current_time_ns
#undef timer_join_if_needed
#undef NSEC_PER_MSEC
#undef NSEC_PER_SEC
#undef g_timer_manager
#undef shutdown_initiate
#undef timer_thread_func
#undef calculate_elapsed_ms
#undef interruptible_sleep_ms
#undef timer_cleanup
#undef timer_is_active
#undef timer_remaining_ms
#undef timer_cancel
#undef timer_start
#undef timer_init

static inline bool timer_test_atomic_load_bool(const atomic_bool* value) {
    return atomic_load_explicit(value, memory_order_acquire);
}

static inline void timer_test_atomic_store_bool(atomic_bool* target, bool value) {
    atomic_store_explicit(target, value, memory_order_release);
}

static inline uint64_t timer_test_atomic_load_u64(const _Atomic uint64_t* value) {
    return atomic_load_explicit(value, memory_order_acquire);
}

static inline void timer_test_atomic_store_u64(_Atomic uint64_t* target, uint64_t value) {
    atomic_store_explicit(target, value, memory_order_release);
}

void timer_test_control_reset_clock_queue(void) {
    atomic_store_explicit(&g_clock_fail_count, 0, memory_order_release);
    atomic_store_explicit(&g_clock_fail_errno, 0, memory_order_release);
    atomic_store_explicit(&g_clock_value_index, 0, memory_order_release);
    atomic_store_explicit(&g_clock_value_count, 0, memory_order_release);
    memset(g_clock_values, 0, sizeof(g_clock_values));
}

void timer_test_control_enqueue_clock_time(struct timespec value) {
    int index = atomic_load_explicit(&g_clock_value_count, memory_order_acquire);
    if (index < TIMER_TEST_CLOCK_QUEUE_MAX) {
        g_clock_values[index] = value;
        atomic_store_explicit(&g_clock_value_count, index + 1, memory_order_release);
    }
}

void timer_test_control_fail_clock_gettime(int count, int error_code) {
    atomic_store_explicit(&g_clock_fail_count, count, memory_order_release);
    atomic_store_explicit(&g_clock_fail_errno, error_code, memory_order_release);
}

void timer_test_control_reset_decrement_metrics(void) {
    atomic_store_explicit(&g_decrement_retry_count, 0, memory_order_release);
}

int timer_test_control_get_decrement_retry_count(void) {
    return atomic_load_explicit(&g_decrement_retry_count, memory_order_acquire);
}

void timer_test_control_consume_clock_failure(void) {
    timer_test_decrement_if_positive(&g_clock_fail_count);
}

void timer_test_control_reset_nanosleep(void) {
    atomic_store_explicit(&g_nanosleep_fail_count, 0, memory_order_release);
    atomic_store_explicit(&g_nanosleep_error, 0, memory_order_release);
    atomic_store_explicit(&g_nanosleep_populate_remaining, false, memory_order_release);
}

void timer_test_control_fail_nanosleep(int count, int error_code, bool populate_remaining) {
    atomic_store_explicit(&g_nanosleep_fail_count, count, memory_order_release);
    atomic_store_explicit(&g_nanosleep_error, error_code, memory_order_release);
    atomic_store_explicit(&g_nanosleep_populate_remaining, populate_remaining, memory_order_release);
}

void timer_test_control_reset_pthread(void) {
    atomic_store_explicit(&g_pthread_create_fail_count, 0, memory_order_release);
    atomic_store_explicit(&g_pthread_create_error, 0, memory_order_release);
    atomic_store_explicit(&g_pthread_join_count, 0, memory_order_release);
    atomic_store_explicit(&g_pthread_create_run_inline, false, memory_order_release);
}

void timer_test_control_fail_pthread_create(int count, int error_code) {
    atomic_store_explicit(&g_pthread_create_fail_count, count, memory_order_release);
    atomic_store_explicit(&g_pthread_create_error, error_code, memory_order_release);
}

int timer_test_control_get_pthread_join_count(void) {
    return atomic_load_explicit(&g_pthread_join_count, memory_order_acquire);
}

void timer_test_control_set_pthread_inline(bool enable) {
    atomic_store_explicit(&g_pthread_create_run_inline, enable, memory_order_release);
}

void timer_test_control_set_manager_state(timer_test_manager_state_t state) {
    timer_test_atomic_store_bool(&g_timer_manager_whitebox.initialized, state.initialized);
    timer_test_atomic_store_bool(&g_timer_manager_whitebox.active, state.active);
    timer_test_atomic_store_bool(&g_timer_manager_whitebox.stop_requested, state.stop_requested);
    timer_test_atomic_store_bool(&g_timer_manager_whitebox.thread_joinable, state.thread_joinable);
    timer_test_atomic_store_u64(&g_timer_manager_whitebox.duration_ms, state.duration_ms);
    timer_test_atomic_store_u64(&g_timer_manager_whitebox.start_ns, state.start_ns);
}

timer_test_manager_state_t timer_test_control_get_manager_state(void) {
    timer_test_manager_state_t state = {0};
    state.initialized = timer_test_atomic_load_bool(&g_timer_manager_whitebox.initialized);
    state.active = timer_test_atomic_load_bool(&g_timer_manager_whitebox.active);
    state.stop_requested = timer_test_atomic_load_bool(&g_timer_manager_whitebox.stop_requested);
    state.thread_joinable = timer_test_atomic_load_bool(&g_timer_manager_whitebox.thread_joinable);
    state.duration_ms = timer_test_atomic_load_u64(&g_timer_manager_whitebox.duration_ms);
    state.start_ns = timer_test_atomic_load_u64(&g_timer_manager_whitebox.start_ns);
    return state;
}

void timer_test_control_reset(void) {
    timer_test_control_reset_clock_queue();
    timer_test_control_reset_nanosleep();
    timer_test_control_reset_pthread();

    timer_test_manager_state_t default_state = {
        .initialized = false,
        .active = false,
        .stop_requested = false,
        .thread_joinable = false,
        .duration_ms = 0,
        .start_ns = 0,
    };
    timer_test_control_set_manager_state(default_state);
}
