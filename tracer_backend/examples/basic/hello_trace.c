/**
 * hello_trace.c - Basic ADA tracing example
 * 
 * This example demonstrates the simplest use case of ADA tracing.
 * It shows function calls with timing delays for visibility.
 */

#include <stdio.h>
#include <unistd.h>

void greet(const char* name) {
    printf("Hello, %s!\n", name);
    usleep(100000);  // 100ms delay for trace visibility
}

void farewell(const char* name) {
    printf("Goodbye, %s!\n", name);
    usleep(50000);   // 50ms delay
}

int main() {
    printf("Starting hello_trace example...\n");
    
    // Call greet function multiple times
    greet("World");
    greet("ADA");
    greet("Tracer");
    
    // Add a farewell
    farewell("Everyone");
    
    printf("Example completed.\n");
    return 0;
}
