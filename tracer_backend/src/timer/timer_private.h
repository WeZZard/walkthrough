#ifndef TRACER_BACKEND_SRC_TIMER_TIMER_PRIVATE_H
#define TRACER_BACKEND_SRC_TIMER_TIMER_PRIVATE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TIMER_MONITOR_INTERVAL_MS 100ULL

typedef struct timer_manager {
    pthread_t thread;
    atomic_bool initialized;
    atomic_bool active;
    atomic_bool stop_requested;
    atomic_bool thread_joinable;
    _Atomic uint64_t duration_ms;
    _Atomic uint64_t start_ns;
} timer_manager_t;

bool interruptible_sleep_ms(uint64_t sleep_ms);
uint64_t calculate_elapsed_ms(uint64_t start_ns);
void* timer_thread_func(void* ctx);

#endif  // TRACER_BACKEND_SRC_TIMER_TIMER_PRIVATE_H
