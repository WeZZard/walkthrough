/*
 * signal_trace.c - Signal handling ADA tracing example
 *
 * Demonstrates how asynchronous signals interact with application control flow.
 * A loop logs periodic heartbeats while responding to SIGINT and SIGTERM. The
 * SIGINT handler counts invocations (terminating after three), and the SIGTERM
 * handler requests a graceful shutdown with explicit cleanup logging.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t sigint_count = 0;
static volatile sig_atomic_t report_sigint = 0;
static volatile sig_atomic_t sigterm_received = 0;

static void handle_sigint(int signo) {
    (void)signo;
    ++sigint_count;
    report_sigint = 1;
    if (sigint_count >= 3) {
        running = 0;
    }
}

static void handle_sigterm(int signo) {
    (void)signo;
    sigterm_received = 1;
    running = 0;
}

static void install_handler(int signal_number, void (*handler)(int)) {
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = handler;
    if (sigaction(signal_number, &action, NULL) != 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    puts("Signal tracing example: waiting for SIGINT or SIGTERM.");

    install_handler(SIGINT, handle_sigint);
    install_handler(SIGTERM, handle_sigterm);

    while (running) {
        if (report_sigint) {
            printf("SIGINT handler invoked (%d/3).\n", sigint_count);
            fflush(stdout);
            report_sigint = 0;
            if (sigint_count >= 3) {
                puts("SIGINT threshold reached; exiting main loop.");
                fflush(stdout);
            }
        }

        if (sigterm_received) {
            puts("SIGTERM received - performing cleanup.");
            fflush(stdout);
            break;
        }

        puts("Status heartbeat: application is idle.");
        fflush(stdout);

        unsigned int remaining = 2U;
        while (remaining > 0U && running) {
            remaining = sleep(remaining);
        }
    }

    if (sigterm_received) {
        puts("Cleanup complete - terminating after SIGTERM.");
        fflush(stdout);
    } else if (sigint_count >= 3) {
        puts("Graceful exit after handling three SIGINT signals.");
        fflush(stdout);
    } else {
        puts("Shutdown requested without signal trigger.");
        fflush(stdout);
    }

    return 0;
}

