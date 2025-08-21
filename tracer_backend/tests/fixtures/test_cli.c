// Simple CLI test program for tracing
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

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
    printf("Test CLI Program Started (PID: %d)\n", getpid());
    
    // Give time for tracer to attach
    if (argc > 1 && strcmp(argv[1], "--wait") == 0) {
        printf("Waiting for tracer to attach...\n");
        sleep(2);
    }
    
    printf("\n=== Testing Fibonacci ===\n");
    for (int i = 0; i < 10; i++) {
        int result = fibonacci(i);
        printf("fibonacci(%d) = %d\n", i, result);
    }
    
    printf("\n=== Testing File Operations ===\n");
    process_file("/etc/hosts");
    process_file("/etc/passwd");
    
    printf("\n=== Testing Math Operations ===\n");
    double pi = calculate_pi(10000);
    printf("Calculated PI: %.10f\n", pi);
    printf("Actual PI:     %.10f\n", M_PI);
    printf("Error:         %.10f\n", fabs(pi - M_PI));
    
    printf("\n=== Testing Recursion ===\n");
    recursive_function(5);
    
    printf("\n=== Testing Memory Operations ===\n");
    for (int i = 0; i < 5; i++) {
        size_t size = (1 << i) * 1024;
        void* mem = malloc(size);
        printf("Allocated %zu bytes at %p\n", size, mem);
        memset(mem, i, size);
        free(mem);
    }
    
    printf("\nTest CLI Program Completed\n");
    return 0;
}