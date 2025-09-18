// Simple CLI test program for tracing
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_cli_modes.h"

// Wrapper for malloc that can be overridden for testing
// By default, uses standard malloc
static void* (*test_malloc_impl)(size_t) = malloc;

// Allow tests to override malloc behavior
void test_cli_set_malloc_impl(void* (*impl)(size_t)) {
    test_malloc_impl = impl;
}

// Wrapper function used in the code
static void* test_malloc(size_t size) {
    return test_malloc_impl(size);
}

// Some functions to trace
int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

void process_file(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Read %zd bytes: %.50s...\n", bytes, buffer);
    }
    
    close(fd);
}

double calculate_pi(int iterations) {
    double pi = 0.0;
    for (int i = 0; i < iterations; i++) {
        double term = 4.0 / (2 * i + 1);
        if (i % 2 == 0) {
            pi += term;
        } else {
            pi -= term;
        }
    }
    return pi;
}

void recursive_function(int depth) {
    if (depth <= 0) return;
    
    printf("Depth: %d\n", depth);
    recursive_function(depth - 1);
}

int main(int argc, char* argv[]) {
    TestCliOptions options;
    test_cli_parse_args(argc, argv, &options);
    TestCliWorkload workload = test_cli_workload_from_options(&options);

    printf("Test CLI Program Started (PID: %d)\n", getpid());

    if (options.brief_mode) {
        printf("Running in brief workload mode\n");
    }

    // Give time for tracer to attach when requested
    if (options.wait_for_attach) {
        printf("Waiting for tracer to attach...\n");
        sleep(2);
    }

    printf("\n=== Testing Fibonacci ===\n");
    for (int i = 0; i < workload.fibonacci_terms; i++) {
        int result = fibonacci(i);
        printf("fibonacci(%d) = %d\n", i, result);
    }

    printf("\n=== Testing File Operations ===\n");
    const char* files[] = {"/etc/hosts", "/etc/passwd"};
    int max_files = (int)(sizeof(files) / sizeof(files[0]));
    for (int i = 0; i < workload.file_operations && i < max_files; i++) {
        process_file(files[i]);
    }

    printf("\n=== Testing Math Operations ===\n");
    double pi = calculate_pi(workload.pi_iterations);
    printf("Calculated PI: %.10f\n", pi);
    printf("Actual PI:     %.10f\n", M_PI);
    printf("Error:         %.10f\n", fabs(pi - M_PI));

    printf("\n=== Testing Recursion ===\n");
    recursive_function(workload.recursion_depth);

    printf("\n=== Testing Memory Operations ===\n");
    for (int i = 0; i < workload.memory_allocations; i++) {
        size_t size = (size_t)(1 << i) * 1024;
        void* mem = test_malloc(size);
        if (mem != NULL) {
            printf("Allocated %zu bytes at %p\n", size, mem);
            memset(mem, i, size);
            free(mem);
        } else {
            printf("Allocation of %zu bytes failed\n", size);
        }
    }

    printf("\nTest CLI Program Completed\n");
    return 0;
}
