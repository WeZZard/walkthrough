#include <cstdarg>
#include <cstring>
#include <gtest/gtest.h>
#include <string>

extern "C" {
#include <tracer_backend/controller/cli_usage.h>
}

namespace {

extern "C" int cli_usage_test_snprintf_failure(char* /*buffer*/, size_t /*buffer_size*/, const char* /*format*/, ...) {
    return -1;
}

}  // namespace

extern "C" {
#define snprintf cli_usage_test_snprintf_failure
#define tracer_controller_format_usage tracer_controller_format_usage_snprintf_failure
#include "../../../src/controller/cli_usage.c"
#undef tracer_controller_format_usage
#undef snprintf
}

TEST(controller__usage__includes_duration_flag, prints_duration_option) {
    char buffer[1024];
    size_t written = tracer_controller_format_usage(buffer, sizeof(buffer), "tracer");

    ASSERT_GT(written, 0u);

    std::string usage(buffer);
    EXPECT_NE(usage.find("--duration <sec>"), std::string::npos);
    EXPECT_NE(usage.find("tracer spawn ./test_cli --wait"), std::string::npos);
}

TEST(cli_usage__null_buffer__then_returns_zero, behavior) {
    size_t written = tracer_controller_format_usage(nullptr, 16, "prog");

    EXPECT_EQ(written, 0u);
}

TEST(cli_usage__zero_buffer_size__then_preserves_buffer, behavior) {
    char buffer[4] = {'A', 'B', '\0', '\0'};

    size_t written = tracer_controller_format_usage(buffer, 0, "prog");

    EXPECT_EQ(written, 0u);
    EXPECT_EQ(buffer[0], 'A');
}

TEST(cli_usage__null_program__then_clears_buffer, behavior) {
    char buffer[4] = {'A', 'B', 'C', '\0'};

    size_t written = tracer_controller_format_usage(buffer, sizeof(buffer), nullptr);

    EXPECT_EQ(written, 0u);
    EXPECT_EQ(buffer[0], '\0');
}

TEST(cli_usage__snprintf_failure__then_returns_zero_and_clears_buffer, behavior) {
    char buffer[32];
    std::memset(buffer, 'X', sizeof(buffer));

    size_t written = tracer_controller_format_usage_snprintf_failure(buffer, sizeof(buffer), "prog");

    EXPECT_EQ(written, 0u);
    EXPECT_EQ(buffer[0], '\0');
}

TEST(cli_usage__insufficient_buffer__then_truncates_and_terminates, behavior) {
    char buffer[16];
    std::memset(buffer, 'X', sizeof(buffer));

    size_t written = tracer_controller_format_usage(buffer, sizeof(buffer), "prog");

    EXPECT_GE(written, sizeof(buffer));
    EXPECT_EQ(buffer[sizeof(buffer) - 1], '\0');
}
