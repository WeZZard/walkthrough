#include "test_cli_modes.h"

#include <assert.h>
#include <string.h>

typedef struct {
    TestCliWorkload normal;
    TestCliWorkload brief;
} TestCliWorkloadProfiles;

static TestCliWorkloadProfiles get_workload_profiles(void) {
    TestCliWorkloadProfiles profiles = {
        .normal = {
            .fibonacci_terms = 10,
            .pi_iterations = 10000,
            .recursion_depth = 5,
            .memory_allocations = 5,
            .file_operations = 2,
        },
        .brief = {
            .fibonacci_terms = 5,
            .pi_iterations = 400,
            .recursion_depth = 2,
            .memory_allocations = 3,
            .file_operations = 1,
        },
    };
    return profiles;
}

void test_cli_parse_args(int argc, char *argv[], TestCliOptions *options) {
    assert(options != NULL);

    options->wait_for_attach = false;
    options->brief_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg == NULL) {
            continue;
        }
        if (strcmp(arg, "--wait") == 0) {
            options->wait_for_attach = true;
        } else if (strcmp(arg, "--brief") == 0) {
            options->brief_mode = true;
        }
    }
}

TestCliWorkload test_cli_workload_from_options(const TestCliOptions *options) {
    TestCliWorkloadProfiles profiles = get_workload_profiles();

    if (options == NULL || !options->brief_mode) {
        return profiles.normal;
    }
    return profiles.brief;
}
