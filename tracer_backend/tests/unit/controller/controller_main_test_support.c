#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <tracer_backend/controller/cli_usage.h>
#include <tracer_backend/controller/shutdown.h>
#include <tracer_backend/utils/tracer_types.h>

#define TIMER_SEQUENCE_MAX 16
#define STATE_SEQUENCE_MAX 16
#define STATS_SEQUENCE_MAX 8

static size_t g_format_usage_call_count = 0;
static size_t g_format_usage_return_value = 0;
static char g_format_usage_payload[1024] = {0};
static int g_fputs_call_count = 0;
static char g_fputs_payload[2048] = {0};

#define FRIDA_CONTROLLER_H

typedef struct FridaController {
  int unused;
} FridaController;

typedef struct {
  int timer_init_calls;
  int timer_init_result;
  int timer_start_calls;
  int timer_start_result;
  uint64_t timer_start_last_duration;
  int timer_cancel_calls;
  int timer_cancel_result;
  int timer_cleanup_calls;
  int timer_is_active_calls;
  bool timer_is_active_values[TIMER_SEQUENCE_MAX];
  size_t timer_is_active_len;
  size_t timer_is_active_index;
  bool timer_is_active_default;

  int sleep_calls;
  int sleep_break_after;

  int system_calls;
  int system_result;
  char system_command[512];

  int frida_create_calls;
  bool frida_create_should_fail;
  int frida_spawn_calls;
  int frida_spawn_result;
  int frida_attach_calls;
  int frida_attach_result;
  int frida_detach_calls;
  int frida_detach_result;
  int frida_resume_calls;
  int frida_resume_result;
  int frida_install_hooks_calls;
  int frida_install_hooks_result;

  ProcessState state_values[STATE_SEQUENCE_MAX];
  size_t state_len;
  size_t state_index;
  ProcessState state_default;

  TracerStats stats_values[STATS_SEQUENCE_MAX];
  size_t stats_len;
  size_t stats_index;
  TracerStats stats_default;

  int shutdown_manager_init_result;
  int signal_handler_init_result;
  int signal_handler_install_result;

  int printf_call_count;
  int fprintf_call_count;
  char last_printf_format[256];
  char last_fprintf_format[256];

  int shutdown_manager_get_last_reason_result;
  bool shutdown_manager_is_shutdown_requested_result;
} ControllerMainTestState;

static const ControllerMainTestState kDefaultTestState = {
    .timer_init_calls = 0,
    .timer_init_result = 0,
    .timer_start_calls = 0,
    .timer_start_result = 0,
    .timer_start_last_duration = 0,
    .timer_cancel_calls = 0,
    .timer_cancel_result = 0,
    .timer_cleanup_calls = 0,
    .timer_is_active_calls = 0,
    .timer_is_active_values = {0},
    .timer_is_active_len = 0,
    .timer_is_active_index = 0,
    .timer_is_active_default = false,
    .sleep_calls = 0,
    .sleep_break_after = 0,
    .system_calls = 0,
    .system_result = 0,
    .system_command = {0},
    .frida_create_calls = 0,
    .frida_create_should_fail = false,
    .frida_spawn_calls = 0,
    .frida_spawn_result = 0,
    .frida_attach_calls = 0,
    .frida_attach_result = 0,
    .frida_detach_calls = 0,
    .frida_detach_result = 0,
    .frida_resume_calls = 0,
    .frida_resume_result = 0,
    .frida_install_hooks_calls = 0,
    .frida_install_hooks_result = 0,
    .state_values = {0},
    .state_len = 0,
    .state_index = 0,
    .state_default = PROCESS_STATE_RUNNING,
    .stats_values = {0},
    .stats_len = 0,
    .stats_index = 0,
    .stats_default = {0},
    .shutdown_manager_init_result = 0,
    .signal_handler_init_result = 0,
    .signal_handler_install_result = 0,
    .printf_call_count = 0,
    .fprintf_call_count = 0,
    .last_printf_format = {0},
    .last_fprintf_format = {0},
    .shutdown_manager_get_last_reason_result = 0,
    .shutdown_manager_is_shutdown_requested_result = false,
};

static ControllerMainTestState g_test_state = {
    .timer_init_calls = 0,
    .timer_init_result = 0,
    .timer_start_calls = 0,
    .timer_start_result = 0,
    .timer_start_last_duration = 0,
    .timer_cancel_calls = 0,
    .timer_cancel_result = 0,
    .timer_cleanup_calls = 0,
    .timer_is_active_calls = 0,
    .timer_is_active_values = {0},
    .timer_is_active_len = 0,
    .timer_is_active_index = 0,
    .timer_is_active_default = false,
    .sleep_calls = 0,
    .sleep_break_after = 0,
    .system_calls = 0,
    .system_result = 0,
    .system_command = {0},
    .frida_create_calls = 0,
    .frida_create_should_fail = false,
    .frida_spawn_calls = 0,
    .frida_spawn_result = 0,
    .frida_attach_calls = 0,
    .frida_attach_result = 0,
    .frida_detach_calls = 0,
    .frida_detach_result = 0,
    .frida_resume_calls = 0,
    .frida_resume_result = 0,
    .frida_install_hooks_calls = 0,
    .frida_install_hooks_result = 0,
    .state_values = {0},
    .state_len = 0,
    .state_index = 0,
    .state_default = PROCESS_STATE_RUNNING,
    .stats_values = {0},
    .stats_len = 0,
    .stats_index = 0,
    .stats_default = {0},
    .shutdown_manager_init_result = 0,
    .signal_handler_init_result = 0,
    .signal_handler_install_result = 0,
    .printf_call_count = 0,
    .fprintf_call_count = 0,
    .last_printf_format = {0},
    .last_fprintf_format = {0},
    .shutdown_manager_get_last_reason_result = 0,
    .shutdown_manager_is_shutdown_requested_result = false,
};

static bool controller_main_test_consume_timer_is_active(void) {
  g_test_state.timer_is_active_calls++;
  if (g_test_state.timer_is_active_index < g_test_state.timer_is_active_len) {
    return g_test_state
        .timer_is_active_values[g_test_state.timer_is_active_index++];
  }
  return g_test_state.timer_is_active_default;
}

static ProcessState controller_main_test_consume_state(void) {
  if (g_test_state.state_index < g_test_state.state_len) {
    return g_test_state.state_values[g_test_state.state_index++];
  }
  return g_test_state.state_default;
}

static TracerStats controller_main_test_consume_stats(void) {
  if (g_test_state.stats_index < g_test_state.stats_len) {
    return g_test_state.stats_values[g_test_state.stats_index++];
  }
  return g_test_state.stats_default;
}

FridaController *frida_controller_create(const char * /*output_dir*/) {
  g_test_state.frida_create_calls++;
  if (g_test_state.frida_create_should_fail) {
    return NULL;
  }
  static FridaController controller;
  return &controller;
}

void frida_controller_destroy(FridaController * /*controller*/) {}

int frida_controller_spawn_suspended(FridaController * /*controller*/,
                                     const char * /*path*/,
                                     char *const /*argv*/[],
                                     uint32_t *out_pid) {
  g_test_state.frida_spawn_calls++;
  if (g_test_state.frida_spawn_result != 0) {
    return g_test_state.frida_spawn_result;
  }
  if (out_pid != NULL) {
    *out_pid = 1234;
  }
  return 0;
}

int frida_controller_attach(FridaController * /*controller*/,
                            uint32_t /*pid*/) {
  g_test_state.frida_attach_calls++;
  if (g_test_state.frida_attach_result != 0) {
    return g_test_state.frida_attach_result;
  }
  return 0;
}

int frida_controller_detach(FridaController * /*controller*/) {
  g_test_state.frida_detach_calls++;
  if (g_test_state.frida_detach_result != 0) {
    return g_test_state.frida_detach_result;
  }
  return 0;
}

int frida_controller_resume(FridaController * /*controller*/) {
  g_test_state.frida_resume_calls++;
  if (g_test_state.frida_resume_result != 0) {
    return g_test_state.frida_resume_result;
  }
  return 0;
}

int frida_controller_install_hooks(FridaController * /*controller*/) {
  g_test_state.frida_install_hooks_calls++;
  if (g_test_state.frida_install_hooks_result != 0) {
    return g_test_state.frida_install_hooks_result;
  }
  return 0;
}

TracerStats frida_controller_get_stats(FridaController * /*controller*/) {
  return controller_main_test_consume_stats();
}

ProcessState frida_controller_get_state(FridaController * /*controller*/) {
  return controller_main_test_consume_state();
}

int timer_init(void) {
  g_test_state.timer_init_calls++;
  return g_test_state.timer_init_result;
}

int timer_start(uint64_t duration_ms) {
  g_test_state.timer_start_calls++;
  g_test_state.timer_start_last_duration = duration_ms;
  return g_test_state.timer_start_result;
}

int timer_cancel(void) {
  g_test_state.timer_cancel_calls++;
  return g_test_state.timer_cancel_result;
}

bool timer_is_active(void) {
  return controller_main_test_consume_timer_is_active();
}

void timer_cleanup(void) { g_test_state.timer_cleanup_calls++; }

uint64_t timer_remaining_ms(void) { return 0; }

size_t tracer_controller_format_usage(char *buffer, size_t buffer_size,
                                      const char * /*program*/) {
  g_format_usage_call_count++;

  if (buffer != NULL && buffer_size > 0) {
    if (g_format_usage_return_value > 0) {
      snprintf(buffer, buffer_size, "%s", g_format_usage_payload);
    } else {
      buffer[0] = '\0';
    }
  }

  return g_format_usage_return_value;
}

int controller_test_fputs(const char *s, FILE * /*stream*/) {
  g_fputs_call_count++;
  if (s != NULL) {
    strncpy(g_fputs_payload, s, sizeof(g_fputs_payload) - 1);
    g_fputs_payload[sizeof(g_fputs_payload) - 1] = '\0';
  } else {
    g_fputs_payload[0] = '\0';
  }
  return 0;
}

int controller_test_system(const char *command) {
  g_test_state.system_calls++;
  if (command != NULL) {
    strncpy(g_test_state.system_command, command,
            sizeof(g_test_state.system_command) - 1);
    g_test_state.system_command[sizeof(g_test_state.system_command) - 1] = '\0';
  } else {
    g_test_state.system_command[0] = '\0';
  }
  return g_test_state.system_result;
}

unsigned int controller_test_sleep(unsigned int seconds);

int controller_test_shutdown_manager_init(ShutdownManager* manager,
                                          ShutdownState* state,
                                          ThreadRegistry* registry,
                                          DrainThread* drain,
                                          const ShutdownOps* ops);
int controller_test_signal_handler_init(SignalHandler* handler,
                                        ShutdownManager* manager);
int controller_test_signal_handler_install(SignalHandler* handler);
int controller_test_printf(const char* format, ...);
int controller_test_fprintf(FILE* stream, const char* format, ...);
int controller_test_shutdown_manager_get_last_reason(const ShutdownManager* manager);
bool controller_test_shutdown_manager_is_shutdown_requested(const ShutdownManager* manager);

#define fputs controller_test_fputs
#define system controller_test_system
#define sleep controller_test_sleep
#define main controller_main_entry
#define shutdown_manager_init controller_test_shutdown_manager_init
#define signal_handler_init controller_test_signal_handler_init
#define signal_handler_install controller_test_signal_handler_install
#define printf controller_test_printf
#define fprintf controller_test_fprintf
#define shutdown_manager_get_last_reason controller_test_shutdown_manager_get_last_reason
#define shutdown_manager_is_shutdown_requested controller_test_shutdown_manager_is_shutdown_requested
#include "../../../src/controller/main.c"
#undef main
#undef sleep
#undef system
#undef fputs
#undef shutdown_manager_init
#undef signal_handler_init
#undef signal_handler_install
#undef printf
#undef fprintf
#undef shutdown_manager_get_last_reason
#undef shutdown_manager_is_shutdown_requested

unsigned int controller_test_sleep(unsigned int seconds) {
  (void)seconds;
  g_test_state.sleep_calls++;
  if (g_test_state.sleep_break_after > 0 &&
      g_test_state.sleep_calls >= g_test_state.sleep_break_after) {
    if (g_shutdown_initialized) {
      if (shutdown_manager_request_shutdown(&g_shutdown_manager,
                                            SHUTDOWN_REASON_MANUAL,
                                            0)) {
        announce_shutdown_if_needed(SHUTDOWN_REASON_MANUAL);
      }
    }
  }
  return 0;
}

void controller_main_test_reset_state(void) {
  g_test_state = kDefaultTestState;
  g_test_state.system_command[0] = '\0';
  g_format_usage_call_count = 0;
  g_format_usage_return_value = 0;
  g_format_usage_payload[0] = '\0';
  g_fputs_call_count = 0;
  g_fputs_payload[0] = '\0';
  memset(&g_shutdown_manager, 0, sizeof(g_shutdown_manager));
  memset(&g_shutdown_state, 0, sizeof(g_shutdown_state));
  memset(&g_signal_handler, 0, sizeof(g_signal_handler));
  g_shutdown_initialized = false;
  g_shutdown_handler_installed = false;
  g_manager_registered = false;
  g_shutdown_announced = false;
}

void controller_main_test_set_format_usage(size_t return_value,
                                           const char *payload) {
  g_format_usage_return_value = return_value;
  if (payload != NULL) {
    strncpy(g_format_usage_payload, payload,
            sizeof(g_format_usage_payload) - 1);
    g_format_usage_payload[sizeof(g_format_usage_payload) - 1] = '\0';
  } else {
    g_format_usage_payload[0] = '\0';
  }
}

size_t controller_main_test_get_format_usage_call_count(void) {
  return g_format_usage_call_count;
}

int controller_main_test_get_fputs_call_count(void) {
  return g_fputs_call_count;
}

const char *controller_main_test_get_fputs_payload(void) {
  return g_fputs_payload;
}

int controller_main_test_get_timer_cancel_calls(void) {
  return g_test_state.timer_cancel_calls;
}

void controller_main_test_set_timer_init_result(int value) {
  g_test_state.timer_init_result = value;
}

int controller_main_test_get_timer_init_calls(void) {
  return g_test_state.timer_init_calls;
}

void controller_main_test_set_timer_start_result(int value) {
  g_test_state.timer_start_result = value;
}

int controller_main_test_get_timer_start_calls(void) {
  return g_test_state.timer_start_calls;
}

uint64_t controller_main_test_get_timer_start_last_duration(void) {
  return g_test_state.timer_start_last_duration;
}

int controller_main_test_get_timer_cleanup_calls(void) {
  return g_test_state.timer_cleanup_calls;
}

void controller_main_test_set_timer_is_active_sequence(const bool *values,
                                                       size_t length) {
  if (values == NULL || length == 0) {
    g_test_state.timer_is_active_len = 0;
    g_test_state.timer_is_active_index = 0;
    return;
  }

  if (length > TIMER_SEQUENCE_MAX) {
    length = TIMER_SEQUENCE_MAX;
  }

  for (size_t i = 0; i < length; ++i) {
    g_test_state.timer_is_active_values[i] = values[i];
  }
  g_test_state.timer_is_active_len = length;
  g_test_state.timer_is_active_index = 0;
}

void controller_main_test_set_timer_is_active_default(bool value) {
  g_test_state.timer_is_active_default = value;
}

void controller_main_test_set_sleep_break_after(int count) {
  g_test_state.sleep_break_after = count;
}

int controller_main_test_get_sleep_calls(void) {
  return g_test_state.sleep_calls;
}

void controller_main_test_set_frida_create_should_fail(bool value) {
  g_test_state.frida_create_should_fail = value;
}

void controller_main_test_set_frida_spawn_result(int value) {
  g_test_state.frida_spawn_result = value;
}

int controller_main_test_get_frida_spawn_calls(void) {
  return g_test_state.frida_spawn_calls;
}

void controller_main_test_set_frida_attach_result(int value) {
  g_test_state.frida_attach_result = value;
}

int controller_main_test_get_frida_attach_calls(void) {
  return g_test_state.frida_attach_calls;
}

void controller_main_test_set_frida_resume_result(int value) {
  g_test_state.frida_resume_result = value;
}

int controller_main_test_get_frida_resume_calls(void) {
  return g_test_state.frida_resume_calls;
}

void controller_main_test_set_frida_install_hooks_result(int value) {
  g_test_state.frida_install_hooks_result = value;
}

int controller_main_test_get_frida_install_hooks_calls(void) {
  return g_test_state.frida_install_hooks_calls;
}

int controller_main_test_get_frida_detach_calls(void) {
  return g_test_state.frida_detach_calls;
}

void controller_main_test_set_frida_state_sequence(const ProcessState *states,
                                                   size_t length) {
  if (states == NULL || length == 0) {
    g_test_state.state_len = 0;
    g_test_state.state_index = 0;
    return;
  }

  if (length > STATE_SEQUENCE_MAX) {
    length = STATE_SEQUENCE_MAX;
  }

  for (size_t i = 0; i < length; ++i) {
    g_test_state.state_values[i] = states[i];
  }
  g_test_state.state_len = length;
  g_test_state.state_index = 0;
}

void controller_main_test_set_frida_stats_sequence(const TracerStats *stats,
                                                   size_t length) {
  if (stats == NULL || length == 0) {
    g_test_state.stats_len = 0;
    g_test_state.stats_index = 0;
    return;
  }

  if (length > STATS_SEQUENCE_MAX) {
    length = STATS_SEQUENCE_MAX;
  }

  for (size_t i = 0; i < length; ++i) {
    g_test_state.stats_values[i] = stats[i];
  }
  g_test_state.stats_len = length;
  g_test_state.stats_index = 0;
}

const char *controller_main_test_get_last_system_command(void) {
  return g_test_state.system_command;
}

int controller_main_test_get_system_calls(void) {
  return g_test_state.system_calls;
}

void controller_main_test_set_system_result(int value) {
  g_test_state.system_result = value;
}

void controller_main_test_set_frida_detach_result(int value) {
  g_test_state.frida_detach_result = value;
}

void controller_main_test_set_timer_cancel_result(int value) {
  g_test_state.timer_cancel_result = value;
}

int controller_test_shutdown_manager_init(ShutdownManager* manager,
                                          ShutdownState* state,
                                          ThreadRegistry* registry,
                                          DrainThread* drain,
                                          const ShutdownOps* ops) {
  if (g_test_state.shutdown_manager_init_result != 0) {
    return g_test_state.shutdown_manager_init_result;
  }
  // Call the real function
  return shutdown_manager_init(manager, state, registry, drain, ops);
}

int controller_test_signal_handler_init(SignalHandler* handler,
                                        ShutdownManager* manager) {
  if (g_test_state.signal_handler_init_result != 0) {
    return g_test_state.signal_handler_init_result;
  }
  // Call the real function
  return signal_handler_init(handler, manager);
}

int controller_test_signal_handler_install(SignalHandler* handler) {
  if (g_test_state.signal_handler_install_result != 0) {
    return g_test_state.signal_handler_install_result;
  }
  // Call the real function
  return signal_handler_install(handler);
}

int controller_test_printf(const char* format, ...) {
  g_test_state.printf_call_count++;
  if (format != NULL) {
    strncpy(g_test_state.last_printf_format, format,
            sizeof(g_test_state.last_printf_format) - 1);
    g_test_state.last_printf_format[sizeof(g_test_state.last_printf_format) - 1] = '\0';
  }
  // Still print to stdout for test visibility
  va_list args;
  va_start(args, format);
  int result = vprintf(format, args);
  va_end(args);
  return result;
}

int controller_test_fprintf(FILE* stream, const char* format, ...) {
  g_test_state.fprintf_call_count++;
  if (format != NULL) {
    strncpy(g_test_state.last_fprintf_format, format,
            sizeof(g_test_state.last_fprintf_format) - 1);
    g_test_state.last_fprintf_format[sizeof(g_test_state.last_fprintf_format) - 1] = '\0';
  }
  // Still print to the stream for test visibility
  va_list args;
  va_start(args, format);
  int result = vfprintf(stream, format, args);
  va_end(args);
  return result;
}

void controller_main_test_set_shutdown_manager_init_result(int value) {
  g_test_state.shutdown_manager_init_result = value;
}

void controller_main_test_set_signal_handler_init_result(int value) {
  g_test_state.signal_handler_init_result = value;
}

void controller_main_test_set_signal_handler_install_result(int value) {
  g_test_state.signal_handler_install_result = value;
}

int controller_main_test_get_printf_call_count(void) {
  return g_test_state.printf_call_count;
}

int controller_main_test_get_fprintf_call_count(void) {
  return g_test_state.fprintf_call_count;
}

const char* controller_main_test_get_last_fprintf_format(void) {
  return g_test_state.last_fprintf_format;
}

int controller_test_shutdown_manager_get_last_reason(const ShutdownManager* manager) {
  (void)manager;
  if (g_test_state.shutdown_manager_get_last_reason_result != 0) {
    return g_test_state.shutdown_manager_get_last_reason_result;
  }
  // Call the real function
  return shutdown_manager_get_last_reason(manager);
}

bool controller_test_shutdown_manager_is_shutdown_requested(const ShutdownManager* manager) {
  (void)manager;
  if (g_test_state.shutdown_manager_is_shutdown_requested_result) {
    return true;
  }
  // Call the real function
  return shutdown_manager_is_shutdown_requested(manager);
}

void controller_main_test_set_shutdown_manager_get_last_reason(int value) {
  g_test_state.shutdown_manager_get_last_reason_result = value;
}

void controller_main_test_set_shutdown_manager_is_shutdown_requested(bool value) {
  g_test_state.shutdown_manager_is_shutdown_requested_result = value;
}
