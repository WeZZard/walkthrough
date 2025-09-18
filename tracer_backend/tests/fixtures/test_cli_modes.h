#ifndef TEST_CLI_MODES_H
#define TEST_CLI_MODES_H

#include <stdbool.h>
#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wait_for_attach;
    bool brief_mode;
} TestCliOptions;

typedef struct {
    int fibonacci_terms;
    int pi_iterations;
    int recursion_depth;
    int memory_allocations;
    int file_operations;
} TestCliWorkload;

void test_cli_parse_args(int argc, char *argv[], TestCliOptions *options);
TestCliWorkload test_cli_workload_from_options(const TestCliOptions *options);

// Test helper to override malloc for testing malloc failures
void test_cli_set_malloc_impl(void* (*impl)(size_t));

#ifdef __cplusplus
}
#endif

#endif // TEST_CLI_MODES_H
