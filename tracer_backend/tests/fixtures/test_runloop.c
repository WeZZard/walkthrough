// RunLoop-based test program for tracing
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>
#include <fcntl.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>

static volatile bool g_running = true;
static int g_counter = 0;

// Timer callback
void timer_callback(CFRunLoopTimerRef timer, void* info) {
    g_counter++;
    printf("[Timer %d] Tick at %.2f\n", g_counter, CFAbsoluteTimeGetCurrent());
    
    // Do some work
    int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }
    
    // Stop after 10 ticks
    if (g_counter >= 10) {
        printf("Stopping run loop...\n");
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}

// Dispatch queue work
void dispatch_work() {
    dispatch_queue_t queue = dispatch_queue_create("com.ada.test", 
                                                   DISPATCH_QUEUE_CONCURRENT);
    
    // Submit some async work
    for (int i = 0; i < 5; i++) {
        dispatch_async(queue, ^{
            printf("[Dispatch %d] Working on thread %p\n", i, pthread_self());
            usleep(100000); // 100ms
            
            // Do some computation
            double result = 0;
            for (int j = 0; j < 10000; j++) {
                result += sqrt(j);
            }
            printf("[Dispatch %d] Result: %.2f\n", i, result);
        });
    }
    
    // Wait for completion
    dispatch_barrier_sync(queue, ^{
        printf("All dispatch work completed\n");
    });
}

// Signal handler
void signal_handler(int sig) {
    printf("Received signal %d, stopping...\n", sig);
    g_running = false;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

// Network simulation
void simulate_network() {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        printf("[Network] Simulating network request...\n");
        sleep(1);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            printf("[Network] Response received on main queue\n");
        });
    });
}

// File monitoring
void monitor_file(const char* path) {
    int fd = open(path, O_EVTONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE,
                                                      fd,
                                                      DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE,
                                                      queue);
    
    dispatch_source_set_event_handler(source, ^{
        unsigned long flags = dispatch_source_get_data(source);
        if (flags & DISPATCH_VNODE_WRITE) {
            printf("[Monitor] File %s was written\n", path);
        }
        if (flags & DISPATCH_VNODE_DELETE) {
            printf("[Monitor] File %s was deleted\n", path);
            dispatch_source_cancel(source);
        }
    });
    
    dispatch_source_set_cancel_handler(source, ^{
        close(fd);
    });
    
    dispatch_resume(source);
}

int main(int argc, char* argv[]) {
    printf("Test RunLoop Program Started (PID: %d)\n", getpid());
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Give time for tracer to attach
    if (argc > 1 && strcmp(argv[1], "--wait") == 0) {
        printf("Waiting for tracer to attach...\n");
        sleep(2);
    }
    
    printf("\n=== Starting RunLoop with Timer ===\n");
    
    // Create a timer that fires every 0.5 seconds
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.5,
        0.5, // interval
        0,   // flags
        0,   // order
        timer_callback,
        NULL
    );
    
    // Add timer to run loop
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    
    printf("\n=== Starting Dispatch Work ===\n");
    dispatch_work();
    
    printf("\n=== Starting Network Simulation ===\n");
    simulate_network();
    
    printf("\n=== Starting File Monitor ===\n");
    monitor_file("/tmp/test_file.txt");
    
    // Create a one-shot timer to demonstrate different timer types
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC),
                  dispatch_get_main_queue(), ^{
        printf("[One-shot] Timer fired after 3 seconds\n");
    });
    
    // Run the main run loop
    printf("\n=== Entering Main RunLoop ===\n");
    CFRunLoopRun();
    
    // Cleanup
    CFRelease(timer);
    
    printf("\nTest RunLoop Program Completed\n");
    return 0;
}