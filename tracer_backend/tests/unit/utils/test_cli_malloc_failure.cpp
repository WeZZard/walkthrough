#include <gtest/gtest.h>
#include <cstddef>
#include <cstdio>
#include <unistd.h>

extern "C" {
#include "../../fixtures/test_cli_modes.h"

// Test helper to override malloc
void test_cli_set_malloc_impl(void* (*impl)(size_t));

// Mock malloc that always fails
void* failing_malloc(size_t) {
    return nullptr;
}

// Include the test_cli.c directly to test its malloc failure path
// This is a special test case to achieve 100% coverage
#define main test_cli_main
#include "../../fixtures/test_cli.c"
#undef main
}

TEST(TestCliMallocFailure, HandlesAllocationFailureGracefully) {
    // Override malloc to always fail
    test_cli_set_malloc_impl(failing_malloc);

    // Prepare arguments for brief mode (fewer allocations)
    char program[] = "test_cli";
    char brief[] = "--brief";
    char* argv[] = {program, brief};

    // Redirect stdout to capture output
    int saved_stdout = dup(STDOUT_FILENO);
    FILE* temp = tmpfile();
    int temp_fd = fileno(temp);
    dup2(temp_fd, STDOUT_FILENO);

    // Call the test_cli main function
    int result = test_cli_main(2, argv);

    // Restore stdout
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    // Read captured output
    rewind(temp);
    char buffer[4096];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, temp);
    buffer[bytes_read] = '\0';
    fclose(temp);

    // Verify that allocation failure messages were printed
    std::string output(buffer);
    EXPECT_NE(output.find("Allocation of"), std::string::npos);
    EXPECT_NE(output.find("failed"), std::string::npos);

    // Verify the program completed successfully despite malloc failures
    EXPECT_EQ(result, 0);

    // Reset malloc to default implementation
    test_cli_set_malloc_impl(malloc);
}