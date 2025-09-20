#include "timer_private.h"

#include <errno.h>
#include <time.h>

#include <tracer_backend/timer/timer.h>

static timer_manager_t g_timer_manager = {0};

static const uint64_t NSEC_PER_SEC = 1000000000ULL;
static const uint64_t NSEC_PER_MSEC = 1000000ULL;

static uint64_t timespec_to_ns(const struct timespec* ts);
static uint64_t current_time_ns(void);
static void timer_join_if_needed(void);

__attribute__((weak)) void shutdown_initiate(void) {
    // Placeholder until the shutdown subsystem is implemented.
}

int timer_init(void) {
    if (atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        return 0;
    }

    timer_join_if_needed();

    atomic_store_explicit(&g_timer_manager.active, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.stop_requested, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.thread_joinable, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.duration_ms, 0, memory_order_release);
    atomic_store_explicit(&g_timer_manager.start_ns, 0, memory_order_release);
    atomic_store_explicit(&g_timer_manager.initialized, true, memory_order_release);
    return 0;
}

int timer_start(uint64_t duration_ms) {
    if (!atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        errno = EINVAL;
        return -1;
    }

    if (duration_ms == 0) {
        errno = EINVAL;
        return -1;
    }

    if (atomic_load_explicit(&g_timer_manager.active, memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    // Join any leftover thread from a previous run before starting a new one.
    timer_join_if_needed();

    atomic_store_explicit(&g_timer_manager.stop_requested, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.duration_ms, duration_ms, memory_order_release);

    uint64_t start_ns = current_time_ns();
    if (start_ns == 0) {
        errno = EFAULT;
        return -1;
    }
    atomic_store_explicit(&g_timer_manager.start_ns, start_ns, memory_order_release);

    pthread_t thread = {0};
    int create_result = pthread_create(&thread, NULL, timer_thread_func, NULL);
    if (create_result != 0) {
        errno = create_result;
        return -1;
    }

    g_timer_manager.thread = thread;
    atomic_store_explicit(&g_timer_manager.thread_joinable, true, memory_order_release);
    atomic_store_explicit(&g_timer_manager.active, true, memory_order_release);
    return 0;
}

int timer_cancel(void) {
    if (!atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        return 0;
    }

    atomic_store_explicit(&g_timer_manager.stop_requested, true, memory_order_release);
    return 0;
}

uint64_t timer_remaining_ms(void) {
    if (!atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        return 0;
    }

    if (!atomic_load_explicit(&g_timer_manager.active, memory_order_acquire)) {
        return 0;
    }

    uint64_t duration_ms = atomic_load_explicit(&g_timer_manager.duration_ms, memory_order_acquire);
    uint64_t start_ns = atomic_load_explicit(&g_timer_manager.start_ns, memory_order_acquire);
    if (duration_ms == 0 || start_ns == 0) {
        return 0;
    }

    uint64_t elapsed_ms = calculate_elapsed_ms(start_ns);
    if (elapsed_ms >= duration_ms) {
        return 0;
    }

    return duration_ms - elapsed_ms;
}

bool timer_is_active(void) {
    if (!atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        return false;
    }

    return atomic_load_explicit(&g_timer_manager.active, memory_order_acquire);
}

void timer_cleanup(void) {
    if (!atomic_load_explicit(&g_timer_manager.initialized, memory_order_acquire)) {
        return;
    }

    timer_cancel();
    timer_join_if_needed();

    atomic_store_explicit(&g_timer_manager.active, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.stop_requested, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.duration_ms, 0, memory_order_release);
    atomic_store_explicit(&g_timer_manager.start_ns, 0, memory_order_release);
    atomic_store_explicit(&g_timer_manager.initialized, false, memory_order_release);
}

bool interruptible_sleep_ms(uint64_t sleep_ms) {
    if (sleep_ms == 0) {
        return true;
    }

    struct timespec request;
    request.tv_sec = (time_t)(sleep_ms / 1000ULL);
    request.tv_nsec = (long)((sleep_ms % 1000ULL) * NSEC_PER_MSEC);

    struct timespec remaining = {0};

    while (nanosleep(&request, &remaining) == -1) {
        if (errno != EINTR) {
            return false;
        }
        if (atomic_load_explicit(&g_timer_manager.stop_requested, memory_order_acquire)) {
            return false;
        }
        request = remaining;
    }

    return true;
}

uint64_t calculate_elapsed_ms(uint64_t start_ns) {
    if (start_ns == 0) {
        return 0;
    }

    uint64_t now_ns = current_time_ns();
    if (now_ns <= start_ns) {
        return 0;
    }

    uint64_t delta_ns = now_ns - start_ns;
    return delta_ns / NSEC_PER_MSEC;
}

void* timer_thread_func(void* ctx) {
    (void)ctx;

    const uint64_t duration_ms = atomic_load_explicit(&g_timer_manager.duration_ms, memory_order_acquire);
    const uint64_t start_ns = atomic_load_explicit(&g_timer_manager.start_ns, memory_order_acquire);

    while (!atomic_load_explicit(&g_timer_manager.stop_requested, memory_order_acquire)) {
        uint64_t elapsed_ms = calculate_elapsed_ms(start_ns);
        if (elapsed_ms >= duration_ms) {
            atomic_store_explicit(&g_timer_manager.active, false, memory_order_release);
            shutdown_initiate();
            return NULL;
        }

        uint64_t remaining_ms = duration_ms - elapsed_ms;
        uint64_t sleep_ms = remaining_ms < TIMER_MONITOR_INTERVAL_MS ? remaining_ms : TIMER_MONITOR_INTERVAL_MS;
        if (sleep_ms == 0) {
            sleep_ms = 1;
        }

        if (!interruptible_sleep_ms(sleep_ms)) {
            break;
        }
    }

    atomic_store_explicit(&g_timer_manager.active, false, memory_order_release);
    atomic_store_explicit(&g_timer_manager.stop_requested, false, memory_order_release);
    return NULL;
}

static void timer_join_if_needed(void) {
    if (!atomic_load_explicit(&g_timer_manager.thread_joinable, memory_order_acquire)) {
        return;
    }

    pthread_join(g_timer_manager.thread, NULL);
    atomic_store_explicit(&g_timer_manager.thread_joinable, false, memory_order_release);
}

static uint64_t current_time_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return timespec_to_ns(&now);
}

static uint64_t timespec_to_ns(const struct timespec* ts) {
    if (ts == NULL) {
        return 0;
    }

    if (ts->tv_sec < 0) {
        return 0;
    }

    if ((uint64_t)ts->tv_sec >= UINT64_MAX / NSEC_PER_SEC) {
        return UINT64_MAX;
    }

    uint64_t sec_component = (uint64_t)ts->tv_sec * NSEC_PER_SEC;
    uint64_t total_ns = sec_component + (uint64_t)ts->tv_nsec;
    if (total_ns < sec_component) {
        return UINT64_MAX;
    }

    return total_ns;
}
