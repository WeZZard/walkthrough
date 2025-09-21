#ifndef TRACER_BACKEND_CONTROLLER_SHUTDOWN_H
#define TRACER_BACKEND_CONTROLLER_SHUTDOWN_H

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DrainThread DrainThread;

typedef enum ShutdownPhase {
    SHUTDOWN_PHASE_IDLE = 0,
    SHUTDOWN_PHASE_SIGNAL_RECEIVED = 1,
    SHUTDOWN_PHASE_STOPPING_THREADS = 2,
    SHUTDOWN_PHASE_DRAINING = 3,
    SHUTDOWN_PHASE_COMPLETED = 4
} ShutdownPhase;

typedef enum ShutdownReason {
    SHUTDOWN_REASON_NONE = 0,
    SHUTDOWN_REASON_SIGNAL = 1,
    SHUTDOWN_REASON_TIMER = 2,
    SHUTDOWN_REASON_MANUAL = 3
} ShutdownReason;

typedef struct ShutdownThreadState {
    _Atomic(bool) accepting_events;
    _Atomic(bool) flush_requested;
    _Atomic(bool) flush_complete;
    _Atomic(uint64_t) pending_events;
} ShutdownThreadState;

typedef struct ShutdownState {
    ShutdownThreadState threads[MAX_THREADS];
    _Atomic(uint32_t) capacity;
    _Atomic(uint32_t) active_threads;
    _Atomic(uint32_t) threads_stopped;
    _Atomic(uint32_t) threads_flushed;
} ShutdownState;

typedef struct ShutdownOps {
    int (*cancel_timer)(void);
    int (*stop_drain)(DrainThread* drain);
} ShutdownOps;

typedef struct ShutdownManager {
    _Atomic(bool) shutdown_requested;
    _Atomic(bool) shutdown_completed;
    _Atomic(unsigned int) phase;
    _Atomic(int) last_signal;
    _Atomic(int) last_reason;
    _Atomic(uint64_t) request_count;
    ShutdownState* state;
    ThreadRegistry* registry;
    DrainThread* drain_thread;
    ShutdownOps ops;
    int wake_read_fd;
    int wake_write_fd;
    bool timestamp_valid;
    struct timespec start_ts;
    struct timespec end_ts;
    uint64_t files_synced;
} ShutdownManager;

typedef struct SignalHandler {
    ShutdownManager* manager;
    struct sigaction previous_sigint;
    struct sigaction previous_sigterm;
    _Atomic(bool) installed;
    _Atomic(uint64_t) signal_count;
} SignalHandler;

void shutdown_state_init(ShutdownState* state, uint32_t capacity);
void shutdown_state_mark_active(ShutdownState* state, uint32_t slot_index);
void shutdown_state_mark_inactive(ShutdownState* state, uint32_t slot_index);
void shutdown_state_record_pending(ShutdownState* state, uint32_t slot_index, uint64_t pending_events);

int shutdown_manager_init(ShutdownManager* manager,
                          ShutdownState* state,
                          ThreadRegistry* registry,
                          DrainThread* drain,
                          const ShutdownOps* ops);

void shutdown_manager_reset(ShutdownManager* manager);
void shutdown_manager_set_registry(ShutdownManager* manager, ThreadRegistry* registry);
void shutdown_manager_set_drain_thread(ShutdownManager* manager, DrainThread* drain);
void shutdown_manager_set_ops(ShutdownManager* manager, const ShutdownOps* ops);
void shutdown_manager_set_wakeup_fds(ShutdownManager* manager, int read_fd, int write_fd);

bool shutdown_manager_request_shutdown(ShutdownManager* manager,
                                       ShutdownReason reason,
                                       int signal_number);

bool shutdown_manager_is_shutdown_requested(const ShutdownManager* manager);
bool shutdown_manager_is_shutdown_complete(const ShutdownManager* manager);
ShutdownPhase shutdown_manager_get_phase(const ShutdownManager* manager);
uint64_t shutdown_manager_get_request_count(const ShutdownManager* manager);
int shutdown_manager_get_last_signal(const ShutdownManager* manager);
int shutdown_manager_get_last_reason(const ShutdownManager* manager);

void shutdown_manager_execute(ShutdownManager* manager);
void shutdown_manager_print_summary(const ShutdownManager* manager);

void shutdown_manager_register_global(ShutdownManager* manager);
void shutdown_manager_unregister_global(void);
void shutdown_manager_signal_wakeup(const ShutdownManager* manager);

int signal_handler_init(SignalHandler* handler, ShutdownManager* manager);
int signal_handler_install(SignalHandler* handler);
void signal_handler_uninstall(SignalHandler* handler);

void shutdown_initiate(void);

#ifdef __cplusplus
}
#endif

#endif  // TRACER_BACKEND_CONTROLLER_SHUTDOWN_H
