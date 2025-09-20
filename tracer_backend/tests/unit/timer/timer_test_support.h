#ifndef TRACER_BACKEND_TESTS_UNIT_TIMER_TIMER_TEST_SUPPORT_H
#define TRACER_BACKEND_TESTS_UNIT_TIMER_TIMER_TEST_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timer_test_manager_state {
    bool initialized;
    bool active;
    bool stop_requested;
    bool thread_joinable;
    uint64_t duration_ms;
    uint64_t start_ns;
} timer_test_manager_state_t;

void timer_test_control_reset(void);

void timer_test_control_fail_clock_gettime(int count, int error_code);
void timer_test_control_reset_clock_queue(void);
void timer_test_control_enqueue_clock_time(struct timespec value);
void timer_test_control_reset_decrement_metrics(void);
int timer_test_control_get_decrement_retry_count(void);
void timer_test_control_consume_clock_failure(void);

void timer_test_control_fail_nanosleep(int count, int error_code, bool populate_remaining);
void timer_test_control_reset_nanosleep(void);

void timer_test_control_fail_pthread_create(int count, int error_code);
void timer_test_control_reset_pthread(void);
int timer_test_control_get_pthread_join_count(void);
void timer_test_control_set_pthread_inline(bool enable);

void timer_test_control_set_manager_state(timer_test_manager_state_t state);
timer_test_manager_state_t timer_test_control_get_manager_state(void);

int timer_init_whitebox(void);
int timer_start_whitebox(uint64_t duration_ms);
int timer_cancel_whitebox(void);
uint64_t timer_remaining_ms_whitebox(void);
bool timer_is_active_whitebox(void);
void timer_cleanup_whitebox(void);
bool interruptible_sleep_ms_whitebox(uint64_t sleep_ms);
uint64_t calculate_elapsed_ms_whitebox(uint64_t start_ns);
void* timer_thread_func_whitebox(void* ctx);
uint64_t timer_test_timespec_to_ns(const struct timespec* ts);
uint64_t timer_test_current_time_ns(void);
void timer_test_timer_join_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif  // TRACER_BACKEND_TESTS_UNIT_TIMER_TIMER_TEST_SUPPORT_H
