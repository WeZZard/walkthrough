/*
 * Test target binary for ada capture integration tests.
 *
 * Usage:
 *   test-target [options]
 *
 * Options:
 *   --crash      Crash with SIGSEGV (null pointer dereference)
 *   --hang       Loop forever (wait to be killed)
 *   --exit <N>   Exit with code N (default: 0)
 *   --sleep <S>  Sleep for S seconds before exiting (default: 2)
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int exit_code = 0;
    int sleep_time = 2;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--crash") == 0) {
            fprintf(stderr, "test-target: triggering crash...\n");
            fflush(stderr);
            /* Null pointer dereference causes SIGSEGV */
            volatile int *null_ptr = NULL;
            *null_ptr = 1;
            return 1; /* Never reached */
        }

        if (strcmp(argv[i], "--hang") == 0) {
            fprintf(stderr, "test-target: hanging forever...\n");
            fflush(stderr);
            while (1) {
                sleep(1);
            }
            return 0; /* Never reached */
        }

        if (strcmp(argv[i], "--exit") == 0 && i + 1 < argc) {
            exit_code = atoi(argv[++i]);
        }

        if (strcmp(argv[i], "--sleep") == 0 && i + 1 < argc) {
            sleep_time = atoi(argv[++i]);
        }
    }

    fprintf(stderr, "test-target: sleeping for %d seconds...\n", sleep_time);
    fflush(stderr);
    sleep(sleep_time);

    fprintf(stderr, "test-target: exiting with code %d\n", exit_code);
    fflush(stderr);
    return exit_code;
}
