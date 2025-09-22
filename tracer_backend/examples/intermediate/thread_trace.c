/*
 * thread_trace.c - Multi-threaded ADA tracing example
 *
 * Demonstrates how tracing tools capture concurrent activity by launching
 * four worker threads that log their progress. Each worker reports five
 * iterations with a small sleep to create observable scheduling behavior.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define WORKER_COUNT 4
#define ITERATION_COUNT 5
#define SLEEP_MICROS 10000

struct worker_args {
    int id;
};

static void *worker_thread(void *arg) {
    const struct worker_args *ctx = (const struct worker_args *)arg;

    for (int iteration = 1; iteration <= ITERATION_COUNT; ++iteration) {
        printf("[worker %d] iteration %d\n", ctx->id, iteration);
        fflush(stdout);
        if (usleep(SLEEP_MICROS) != 0) {
            perror("usleep");
            pthread_exit(NULL);
        }
    }

    return NULL;
}

int main(void) {
    pthread_t threads[WORKER_COUNT];
    struct worker_args args[WORKER_COUNT];

    puts("Thread tracing demo: launching workers...");

    for (int i = 0; i < WORKER_COUNT; ++i) {
        args[i].id = i;
        int rc = pthread_create(&threads[i], NULL, worker_thread, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "Failed to create worker %d: %d\n", i, rc);
            return EXIT_FAILURE;
        }
    }

    puts("Workers running; waiting for completion...");

    for (int i = 0; i < WORKER_COUNT; ++i) {
        int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "Failed to join worker %d: %d\n", i, rc);
            return EXIT_FAILURE;
        }
    }

    puts("All workers have completed.");
    return EXIT_SUCCESS;
}

