#include <gtest/gtest.h>

extern "C" {
#include "../../fixtures/test_cli_modes.h"
}

TEST(TestCliModes, ParsesWaitAndBriefFlags) {
    char program[] = "test_cli";
    char brief[] = "--brief";
    char wait[] = "--wait";
    char* argv[] = {program, brief, wait};

    TestCliOptions options;
    test_cli_parse_args(3, argv, &options);

    EXPECT_TRUE(options.brief_mode);
    EXPECT_TRUE(options.wait_for_attach);
}

TEST(TestCliModes, DefaultsWithoutFlags) {
    char program[] = "test_cli";
    char* argv[] = {program};

    TestCliOptions options;
    test_cli_parse_args(1, argv, &options);

    TestCliWorkload workload = test_cli_workload_from_options(&options);

    EXPECT_FALSE(options.brief_mode);
    EXPECT_FALSE(options.wait_for_attach);
    EXPECT_EQ(workload.fibonacci_terms, 10);
    EXPECT_EQ(workload.pi_iterations, 10000);
    EXPECT_EQ(workload.recursion_depth, 5);
    EXPECT_EQ(workload.memory_allocations, 5);
    EXPECT_EQ(workload.file_operations, 2);
}

TEST(TestCliModes, BriefModeReducesWorkload) {
    char program[] = "test_cli";
    char brief[] = "--brief";
    char* argv[] = {program, brief};

    TestCliOptions options;
    test_cli_parse_args(2, argv, &options);

    TestCliWorkload workload = test_cli_workload_from_options(&options);

    EXPECT_TRUE(options.brief_mode);
    EXPECT_FALSE(options.wait_for_attach);
    EXPECT_LT(workload.fibonacci_terms, 10);
    EXPECT_LT(workload.pi_iterations, 10000);
    EXPECT_LT(workload.recursion_depth, 5);
    EXPECT_LT(workload.memory_allocations, 5);
    EXPECT_LT(workload.file_operations, 2);
    EXPECT_GE(workload.fibonacci_terms, 1);
    EXPECT_GE(workload.pi_iterations, 1);
}
