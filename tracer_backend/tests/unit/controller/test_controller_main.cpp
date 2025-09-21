#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdlib>
#include <string>
#include <vector>

#include <tracer_backend/utils/tracer_types.h>

extern "C" {
void controller_main_test_reset_state(void);
void controller_main_test_set_format_usage(size_t return_value,
                                           const char *payload);
size_t controller_main_test_get_format_usage_call_count(void);
int controller_main_test_get_fputs_call_count(void);
const char *controller_main_test_get_fputs_payload(void);
int controller_main_test_get_timer_cancel_calls(void);
void print_usage(const char *program);
int controller_main_entry(int argc, char *argv[]);

void controller_main_test_set_timer_init_result(int value);
int controller_main_test_get_timer_init_calls(void);
void controller_main_test_set_timer_start_result(int value);
int controller_main_test_get_timer_start_calls(void);
uint64_t controller_main_test_get_timer_start_last_duration(void);
int controller_main_test_get_timer_cleanup_calls(void);
void controller_main_test_set_timer_is_active_sequence(const bool *values,
                                                       size_t length);
void controller_main_test_set_timer_is_active_default(bool value);
void controller_main_test_set_timer_cancel_result(int value);

void controller_main_test_set_sleep_break_after(int count);
int controller_main_test_get_sleep_calls(void);

void controller_main_test_set_frida_create_should_fail(bool value);
void controller_main_test_set_frida_spawn_result(int value);
int controller_main_test_get_frida_spawn_calls(void);
void controller_main_test_set_frida_attach_result(int value);
int controller_main_test_get_frida_attach_calls(void);
void controller_main_test_set_frida_resume_result(int value);
int controller_main_test_get_frida_resume_calls(void);
void controller_main_test_set_frida_install_hooks_result(int value);
int controller_main_test_get_frida_install_hooks_calls(void);
int controller_main_test_get_frida_detach_calls(void);

void controller_main_test_set_frida_state_sequence(const ProcessState *states,
                                                   size_t length);
void controller_main_test_set_frida_stats_sequence(const TracerStats *stats,
                                                   size_t length);

const char *controller_main_test_get_last_system_command(void);
int controller_main_test_get_system_calls(void);
void controller_main_test_set_system_result(int value);

void controller_main_test_set_shutdown_manager_init_result(int value);
void controller_main_test_set_signal_handler_init_result(int value);
void controller_main_test_set_signal_handler_install_result(int value);
int controller_main_test_get_printf_call_count(void);
int controller_main_test_get_fprintf_call_count(void);
const char* controller_main_test_get_last_fprintf_format(void);
void controller_main_test_set_shutdown_manager_get_last_reason(int value);
void controller_main_test_set_shutdown_manager_is_shutdown_requested(bool value);
}

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StrEq;

namespace {
int invoke_main_with_vector(std::vector<std::string> args) {
  std::vector<char *> argv;
  argv.reserve(args.size());
  for (auto &arg : args) {
    argv.push_back(arg.data());
  }
  return controller_main_entry(static_cast<int>(argv.size()), argv.data());
}

int invoke_main(std::initializer_list<const char *> args) {
  std::vector<std::string> owned;
  owned.reserve(args.size());
  for (const char *arg : args) {
    owned.emplace_back(arg);
  }
  return invoke_main_with_vector(std::move(owned));
}
} // namespace

TEST(main__print_usage_success__then_forwards_buffer_to_fputs, behavior) {
  controller_main_test_reset_state();
  const std::string payload = "formatted output";
  controller_main_test_set_format_usage(payload.size(), payload.c_str());

  print_usage("prog");

  EXPECT_EQ(controller_main_test_get_format_usage_call_count(), 1u);
  EXPECT_EQ(controller_main_test_get_fputs_call_count(), 1);
  EXPECT_EQ(std::string(controller_main_test_get_fputs_payload()), payload);
}

TEST(main__print_usage_failure__then_emits_fallback_message, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_format_usage(0, "");

  testing::internal::CaptureStdout();
  print_usage("prog");
  std::string output = testing::internal::GetCapturedStdout();

  EXPECT_EQ(controller_main_test_get_format_usage_call_count(), 1u);
  EXPECT_EQ(controller_main_test_get_fputs_call_count(), 0);
  EXPECT_THAT(output, HasSubstr("Usage: prog <mode> <target> [options]"));
}

TEST(main__argc_too_small__then_prints_usage_and_returns_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_format_usage(0, "");

  testing::internal::CaptureStdout();
  int exit_code = invoke_main({"prog"});
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(out, HasSubstr("Usage: prog <mode>"));
}

TEST(main__duration_non_numeric__then_reports_error, behavior) {
  controller_main_test_reset_state();

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "abc"};

  testing::internal::CaptureStderr();
  int exit_code = invoke_main_with_vector(args);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Invalid duration 'abc'"));
}

TEST(main__duration_negative__then_reports_error, behavior) {
  controller_main_test_reset_state();

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "-5"};

  testing::internal::CaptureStderr();
  int exit_code = invoke_main_with_vector(args);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Invalid duration '-5'"));
}

TEST(main__duration_submillisecond__then_rounds_up_timer, behavior) {
  controller_main_test_reset_state();
  bool timer_sequence[] = {false};
  controller_main_test_set_timer_is_active_sequence(timer_sequence, 1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "0.0001"};

  testing::internal::CaptureStdout();
  int exit_code = invoke_main_with_vector(args);
  testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(controller_main_test_get_timer_start_calls(), 1);
  EXPECT_GE(controller_main_test_get_timer_start_last_duration(), 1u);
}

TEST(main__timer_init_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_timer_init_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to initialize duration timer"));
  EXPECT_EQ(controller_main_test_get_timer_init_calls(), 1);
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 0);
}

TEST(main__timer_start_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_timer_start_result(-1);
  bool timer_sequence[] = {false};
  controller_main_test_set_timer_is_active_sequence(timer_sequence, 1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "1.5"};

  testing::internal::CaptureStderr();
  int exit_code = invoke_main_with_vector(args);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to start duration timer"));
  EXPECT_EQ(controller_main_test_get_timer_start_calls(), 1);
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 1);
}

TEST(main__duration_zero__then_skips_timer_start, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_sleep_break_after(1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "0"};

  testing::internal::CaptureStdout();
  int exit_code = invoke_main_with_vector(args);
  testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(controller_main_test_get_timer_start_calls(), 0);
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 1);
}

TEST(main__cleanup_active_timer__then_cancels_during_cleanup, behavior) {
  controller_main_test_reset_state();
  bool timer_sequence[] = {true, true};
  controller_main_test_set_timer_is_active_sequence(timer_sequence, 2);
  controller_main_test_set_sleep_break_after(1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration",
                                   "2.0"};

  testing::internal::CaptureStdout();
  int exit_code = invoke_main_with_vector(args);
  testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  // Shutdown manager cancels the timer when the stop request is observed and
  // cleanup performs a second cancellation to finish the timer thread.
  EXPECT_EQ(controller_main_test_get_timer_cancel_calls(), 2);
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 1);
}

TEST(main__frida_create_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_create_should_fail(true);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to create controller"));
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 1);
}

TEST(main__spawn_spawn_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_spawn_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to spawn process"));
  EXPECT_EQ(controller_main_test_get_frida_spawn_calls(), 1);
  EXPECT_EQ(controller_main_test_get_frida_attach_calls(), 0);
}

TEST(main__spawn_attach_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_attach_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to attach to process"));
  EXPECT_EQ(controller_main_test_get_frida_spawn_calls(), 1);
  EXPECT_EQ(controller_main_test_get_frida_attach_calls(), 1);
}

TEST(main__attach_invalid_pid__then_reports_error, behavior) {
  controller_main_test_reset_state();

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "attach", "abc"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Invalid PID"));
}

TEST(main__attach_attach_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_attach_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "attach", "123"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to attach to process"));
  EXPECT_EQ(controller_main_test_get_frida_attach_calls(), 1);
}

TEST(main__install_hooks_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_install_hooks_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to install hooks"));
  EXPECT_EQ(controller_main_test_get_frida_install_hooks_calls(), 1);
}

TEST(main__resume_failure__then_reports_error, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_frida_resume_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to resume process"));
  EXPECT_EQ(controller_main_test_get_frida_resume_calls(), 1);
}

TEST(main__unknown_mode__then_prints_usage, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_format_usage(0, "");

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "mystery", "target"});
  std::string err = testing::internal::GetCapturedStderr();
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Unknown mode"));
  EXPECT_THAT(out, HasSubstr("Usage: prog"));
}

TEST(main__exclude_option__then_sets_environment, behavior) {
  controller_main_test_reset_state();
  unsetenv("ADA_EXCLUDE");
  controller_main_test_set_sleep_break_after(1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--exclude",
                                   "symbols.txt"};

  testing::internal::CaptureStdout();
  int exit_code = invoke_main_with_vector(args);
  testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  ASSERT_NE(getenv("ADA_EXCLUDE"), nullptr);
  EXPECT_THAT(std::string(getenv("ADA_EXCLUDE")), StrEq("symbols.txt"));
  EXPECT_EQ(controller_main_test_get_system_calls(), 1);
  EXPECT_THAT(std::string(controller_main_test_get_last_system_command()),
              HasSubstr("mkdir -p"));
  unsetenv("ADA_EXCLUDE");
}

TEST(main__process_terminates__then_exits_monitor_loop, behavior) {
  controller_main_test_reset_state();
  ProcessState states[] = {PROCESS_STATE_FAILED};
  controller_main_test_set_frida_state_sequence(states, 1);
  controller_main_test_set_sleep_break_after(0);

  testing::internal::CaptureStdout();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_THAT(out, HasSubstr("Process has terminated"));
}

TEST(main__successful_spawn_with_duration__then_detaches_and_cleans_up,
     behavior) {
  controller_main_test_reset_state();
  bool timer_sequence[] = {false};
  controller_main_test_set_timer_is_active_sequence(timer_sequence, 1);

  TracerStats stats_sequence[] = {{.events_captured = 10,
                                   .events_dropped = 1,
                                   .bytes_written = 512,
                                   .active_threads = 2,
                                   .hooks_installed = 1}};
  controller_main_test_set_frida_stats_sequence(stats_sequence, 1);

  testing::internal::CaptureStdout();
  int exit_code =
      invoke_main({"prog", "spawn", "target", "--duration", "3.25"});
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_THAT(out, HasSubstr("Duration timer armed"));
  EXPECT_THAT(out, HasSubstr("=== Final Statistics ==="));
  EXPECT_EQ(controller_main_test_get_frida_detach_calls(), 1);
  EXPECT_EQ(controller_main_test_get_timer_cleanup_calls(), 1);
}

TEST(main__shutdown_manager_init_fails__then_reports_error_and_exits, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_shutdown_manager_init_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to initialize shutdown manager"));
  EXPECT_EQ(controller_main_test_get_fprintf_call_count(), 1);
  EXPECT_THAT(std::string(controller_main_test_get_last_fprintf_format()),
              HasSubstr("Failed to initialize shutdown manager"));
}

TEST(main__signal_handler_init_fails__then_reports_error_and_exits, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_signal_handler_init_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to install shutdown signal handlers"));
  EXPECT_EQ(controller_main_test_get_fprintf_call_count(), 1);
  EXPECT_THAT(std::string(controller_main_test_get_last_fprintf_format()),
              HasSubstr("Failed to install shutdown signal handlers"));
}

TEST(main__signal_handler_install_fails__then_reports_error_and_exits, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_signal_handler_install_result(-1);

  testing::internal::CaptureStderr();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(exit_code, 1);
  EXPECT_THAT(err, HasSubstr("Failed to install shutdown signal handlers"));
  EXPECT_EQ(controller_main_test_get_fprintf_call_count(), 1);
  EXPECT_THAT(std::string(controller_main_test_get_last_fprintf_format()),
              HasSubstr("Failed to install shutdown signal handlers"));
}

TEST(main__shutdown_signal_received__then_prints_signal_announcement, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_sleep_break_after(1);
  // Set the last reason to SHUTDOWN_REASON_SIGNAL (1)
  controller_main_test_set_shutdown_manager_get_last_reason(1);
  controller_main_test_set_shutdown_manager_is_shutdown_requested(true);

  // Simulate process running then receiving shutdown signal
  testing::internal::CaptureStdout();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_GT(controller_main_test_get_printf_call_count(), 0);
  EXPECT_THAT(out, HasSubstr("Received shutdown signal, shutting down"));
}

TEST(main__timer_elapsed__then_prints_timer_announcement, behavior) {
  controller_main_test_reset_state();
  // Timer becomes inactive after being active
  bool timer_sequence[] = {false}; // Timer expired
  controller_main_test_set_timer_is_active_sequence(timer_sequence, 1);

  std::vector<std::string> args = {"prog", "spawn", "target", "--duration", "1.0"};

  testing::internal::CaptureStdout();
  int exit_code = invoke_main_with_vector(args);
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  EXPECT_GT(controller_main_test_get_printf_call_count(), 0);
  EXPECT_THAT(out, HasSubstr("Duration elapsed, initiating shutdown"));
}

TEST(main__invalid_shutdown_reason__then_no_announcement_printed, behavior) {
  controller_main_test_reset_state();
  controller_main_test_set_sleep_break_after(1);
  // Set an invalid/unknown shutdown reason (99)
  controller_main_test_set_shutdown_manager_get_last_reason(99);
  controller_main_test_set_shutdown_manager_is_shutdown_requested(true);

  // Simulate process running with unknown shutdown reason
  testing::internal::CaptureStdout();
  int exit_code = invoke_main({"prog", "spawn", "target"});
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(exit_code, 0);
  // Should not print any shutdown announcement for unknown reason
  EXPECT_THAT(out, Not(HasSubstr("shutting down")));
  EXPECT_THAT(out, Not(HasSubstr("initiating shutdown")));
  EXPECT_THAT(out, Not(HasSubstr("stopping")));
}
