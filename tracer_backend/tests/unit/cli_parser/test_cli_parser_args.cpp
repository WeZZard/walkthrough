#include <gtest/gtest.h>

#include <string>

extern "C" {
#include <tracer_backend/cli_parser.h>
}

TEST(cli_parser__spawn_mode_with_leading_flags, then_collects_executable) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--output";
    char arg4[] = "/tmp/out";
    char arg5[] = "./demo";
    char arg6[] = "--";
    char arg7[] = "--child-flag";
    char arg8[] = "arg1";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};
    CLIParser* parser = cli_parser_create(9, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->spawn.argc, 3);
    EXPECT_STREQ("./demo", config->spawn.executable);
    EXPECT_STREQ("./demo", config->spawn.argv[0]);
    EXPECT_STREQ("--child-flag", config->spawn.argv[1]);
    EXPECT_STREQ("arg1", config->spawn.argv[2]);

    EXPECT_TRUE(cli_parse_flags(parser));
    EXPECT_TRUE(config->output.output_specified);
    EXPECT_STREQ("/tmp/out", config->output.output_dir);

    cli_parser_destroy(parser);
}

TEST(cli_parser__attach_mode_with_flags, then_parses_pid_and_name) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "attach";
    char arg3[] = "--output";
    char arg4[] = "/var/tmp";
    char arg5[] = "2048";
    char arg6[] = "process-name";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
    CLIParser* parser = cli_parser_create(7, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_ATTACH);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->attach.pid, 2048);
    EXPECT_STREQ("process-name", config->attach.process_name);

    EXPECT_TRUE(cli_parse_flags(parser));
    EXPECT_TRUE(config->output.output_specified);
    EXPECT_STREQ("/var/tmp", config->output.output_dir);

    cli_parser_destroy(parser);
}

TEST(cli_parser__flag_mixture, then_records_basic_values) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--output=/opt/out";
    char arg4[] = "--duration";
    char arg5[] = "45";
    char arg6[] = "-s128";
    char arg7[] = "--trigger";
    char arg8[] = "symbol=function";
    char arg9[] = "--trigger";
    char arg10[] = "crash";
    char arg11[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11};
    CLIParser* parser = cli_parser_create(12, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->spawn.argc, 1);
    EXPECT_STREQ("./demo", config->spawn.executable);

    EXPECT_TRUE(cli_parse_flags(parser));
    EXPECT_TRUE(config->output.output_specified);
    EXPECT_STREQ("/opt/out", config->output.output_dir);
    EXPECT_TRUE(config->timing.duration_specified);
    EXPECT_EQ(config->timing.duration_seconds, 45u);
    EXPECT_TRUE(config->capture.stack_specified);
    EXPECT_EQ(config->capture.stack_bytes, 128u);
    ASSERT_EQ(config->triggers.count, 2u);
    EXPECT_STREQ("symbol=function", config->triggers.entries[0].raw_value);
    EXPECT_EQ(config->triggers.entries[0].type, TRIGGER_TYPE_SYMBOL);
    EXPECT_STREQ("function", config->triggers.entries[0].symbol_name);
    EXPECT_EQ(config->triggers.entries[0].module_name, nullptr);
    EXPECT_STREQ("crash", config->triggers.entries[1].raw_value);
    EXPECT_EQ(config->triggers.entries[1].type, TRIGGER_TYPE_CRASH);

    cli_parser_destroy(parser);
}

TEST(cli_parser__unknown_flag, then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "./demo";
    char arg4[] = "--unknown";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    CLIParser* parser = cli_parser_create(5, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_FALSE(cli_parse_flags(parser));
    EXPECT_TRUE(cli_parser_has_error(parser));

    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("Unknown flag"), std::string::npos);

    cli_parser_destroy(parser);
}

TEST(cli_parser__mixed_flag_value_styles, then_parses_all) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "-o";
    char arg4[] = "/tmp/out";
    char arg5[] = "--duration=90";
    char arg6[] = "-s";
    char arg7[] = "256";
    char arg8[] = "--stack-bytes=512";
    char arg9[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9};
    CLIParser* parser = cli_parser_create(10, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);

    EXPECT_TRUE(cli_parse_flags(parser));
    EXPECT_TRUE(config->output.output_specified);
    EXPECT_STREQ("/tmp/out", config->output.output_dir);
    EXPECT_TRUE(config->timing.duration_specified);
    EXPECT_EQ(config->timing.duration_seconds, 90u);
    EXPECT_TRUE(config->capture.stack_specified);
    EXPECT_EQ(config->capture.stack_bytes, 512u);

    cli_parser_destroy(parser);
}

TEST(cli_parser__duration_invalid, then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "./demo";
    char arg4[] = "--duration=abc";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    CLIParser* parser = cli_parser_create(5, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_FALSE(cli_parse_flags(parser));
    EXPECT_TRUE(cli_parser_has_error(parser));
    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("duration"), std::string::npos);

    cli_parser_destroy(parser);
}

TEST(cli_parser__stack_bytes_out_of_range, then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "./demo";
    char arg4[] = "--stack-bytes=1024";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    CLIParser* parser = cli_parser_create(5, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_FALSE(cli_parse_flags(parser));
    EXPECT_TRUE(cli_parser_has_error(parser));
    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("stack"), std::string::npos);

    cli_parser_destroy(parser);
}

TEST(cli_parser__persistence_flags, then_store_seconds) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--pre-roll-sec";
    char arg4[] = "15";
    char arg5[] = "--post-roll-sec=25";
    char arg6[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
    CLIParser* parser = cli_parser_create(7, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_TRUE(cli_parse_flags(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    EXPECT_TRUE(config->timing.pre_roll_specified);
    EXPECT_EQ(config->timing.pre_roll_seconds, 15u);
    EXPECT_TRUE(config->timing.post_roll_specified);
    EXPECT_EQ(config->timing.post_roll_seconds, 25u);

    cli_parser_destroy(parser);
}

TEST(cli_parser__trigger_variants, then_parse_details) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--trigger";
    char arg4[] = "symbol=core::main";
    char arg5[] = "--trigger";
    char arg6[] = "time=30";
    char arg7[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    CLIParser* parser = cli_parser_create(8, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_TRUE(cli_parse_flags(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    ASSERT_EQ(config->triggers.count, 2u);

    const TriggerDefinition& symbol_trigger = config->triggers.entries[0];
    EXPECT_EQ(symbol_trigger.type, TRIGGER_TYPE_SYMBOL);
    EXPECT_STREQ("symbol=core::main", symbol_trigger.raw_value);
    EXPECT_STREQ("main", symbol_trigger.symbol_name);
    ASSERT_NE(symbol_trigger.module_name, nullptr);
    EXPECT_STREQ("core", symbol_trigger.module_name);

    const TriggerDefinition& time_trigger = config->triggers.entries[1];
    EXPECT_EQ(time_trigger.type, TRIGGER_TYPE_TIME);
    EXPECT_STREQ("time=30", time_trigger.raw_value);
    EXPECT_EQ(time_trigger.time_seconds, 30u);

    cli_parser_destroy(parser);
}

TEST(cli_parser__trigger_duplicate, then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "./demo";
    char arg4[] = "--trigger";
    char arg5[] = "crash";
    char arg6[] = "--trigger";
    char arg7[] = "crash";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    CLIParser* parser = cli_parser_create(8, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_FALSE(cli_parse_flags(parser));
    EXPECT_TRUE(cli_parser_has_error(parser));
    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("Duplicate"), std::string::npos);

    cli_parser_destroy(parser);
}

TEST(cli_parser__exclude_list, then_splits_modules) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--exclude";
    char arg4[] = "libc.so , libssl.so";
    char arg5[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
    CLIParser* parser = cli_parser_create(6, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_TRUE(cli_parse_flags(parser));

    TracerConfig* config = cli_parser_get_config(parser);
    ASSERT_NE(config, nullptr);
    EXPECT_TRUE(config->filters.exclude_specified);
    ASSERT_EQ(config->filters.count, 2u);
    EXPECT_STREQ("libc.so", config->filters.modules[0]);
    EXPECT_STREQ("libssl.so", config->filters.modules[1]);

    cli_parser_destroy(parser);
}

TEST(cli_parser__exclude_invalid_module, then_sets_error) {
    char arg0[] = "ada";
    char arg1[] = "trace";
    char arg2[] = "spawn";
    char arg3[] = "--exclude";
    char arg4[] = "lib^bad";
    char arg5[] = "./demo";

    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
    CLIParser* parser = cli_parser_create(6, argv);
    ASSERT_NE(parser, nullptr);

    EXPECT_EQ(cli_parser_detect_mode(parser), MODE_SPAWN);
    EXPECT_TRUE(cli_parse_mode_args(parser));

    EXPECT_FALSE(cli_parse_flags(parser));
    EXPECT_TRUE(cli_parser_has_error(parser));
    std::string error = cli_parser_get_error(parser);
    EXPECT_NE(error.find("module"), std::string::npos);

    cli_parser_destroy(parser);
}
