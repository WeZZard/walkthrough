#include <gtest/gtest.h>

#include <string>

extern "C" {
#include <tracer_backend/cli_parser.h>
}

TEST(cli_parser__mode_detection, no_command_then_returns_invalid) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char* argv[] = {arg0, arg1};

    CLIParser* parser = cli_parser_create(2, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_INVALID);
    EXPECT_TRUE(cli_parser_has_error(parser));

    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("No command"), std::string::npos);

    cli_parser_destroy(parser);
}

TEST(cli_parser__mode_detection, spawn_command_then_sets_spawn_mode) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "./app";
    char* argv[] = {arg0, arg1, arg2, arg3};

    CLIParser* parser = cli_parser_create(4, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_SPAWN);
    EXPECT_EQ(parser->current_arg, 3);
    EXPECT_FALSE(cli_parser_has_error(parser));

    cli_parser_destroy(parser);
}

TEST(cli_parser__mode_detection, attach_command_then_sets_attach_mode) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "attach";
    char arg3[] = "1234";
    char* argv[] = {arg0, arg1, arg2, arg3};

    CLIParser* parser = cli_parser_create(4, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_ATTACH);
    EXPECT_EQ(parser->current_arg, 3);
    EXPECT_FALSE(cli_parser_has_error(parser));

    cli_parser_destroy(parser);
}

TEST(cli_parser__mode_detection, help_flag_then_sets_help_mode) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "--help";
    char* argv[] = {arg0, arg1, arg2};

    CLIParser* parser = cli_parser_create(3, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_HELP);
    EXPECT_FALSE(cli_parser_has_error(parser));

    cli_parser_destroy(parser);
}

TEST(cli_parser__mode_detection, version_flag_then_sets_version_mode) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "-v";
    char* argv[] = {arg0, arg1, arg2};

    CLIParser* parser = cli_parser_create(3, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_VERSION);
    EXPECT_FALSE(cli_parser_has_error(parser));

    cli_parser_destroy(parser);
}

TEST(cli_parser__mode_detection, invalid_command_then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "invalid";
    char* argv[] = {arg0, arg1, arg2};

    CLIParser* parser = cli_parser_create(3, argv);
    ASSERT_NE(parser, nullptr);

    ExecutionMode mode = cli_parser_detect_mode(parser);
    EXPECT_EQ(mode, MODE_INVALID);
    EXPECT_TRUE(cli_parser_has_error(parser));

    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("Invalid command"), std::string::npos);

    cli_parser_destroy(parser);
}
