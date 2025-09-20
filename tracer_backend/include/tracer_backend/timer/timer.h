#ifndef TRACER_BACKEND_TIMER_TIMER_H
#define TRACER_BACKEND_TIMER_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the timer subsystem. Safe to call multiple times.
int timer_init(void);

// Starts a duration timer for the requested number of milliseconds.
int timer_start(uint64_t duration_ms);

// Requests cancellation of the active timer if present (async-signal-safe).
int timer_cancel(void);

// Returns the remaining milliseconds for the active timer, or 0 when inactive.
uint64_t timer_remaining_ms(void);

// Returns true if the timer thread is currently active.
bool timer_is_active(void);

// Cancels any active timer and releases resources.
void timer_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif  // TRACER_BACKEND_TIMER_TIMER_H
