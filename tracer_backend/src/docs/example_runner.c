#include "tracer_backend/docs/example_runner.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct tracer_example_runner {
    atomic_uint active_sessions;
    atomic_uint_fast64_t last_duration_ns;
    atomic_uint_fast64_t last_exec_duration_ns;  // Execution time only
};

static uint64_t elapsed_ns(const struct timespec *start, const struct timespec *end) {
    int64_t sec = (int64_t)(end->tv_sec - start->tv_sec);
    int64_t nsec = (int64_t)(end->tv_nsec - start->tv_nsec);
    if (nsec < 0) {
        --sec;
        nsec += 1000000000L;
    }
    if (sec < 0) {
        sec = 0;
    }
    return (uint64_t)sec * UINT64_C(1000000000) + (uint64_t)nsec;
}

static tracer_docs_status_t append_command_output(
    FILE *stream,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written
) {
    size_t total = 0;
    if (stdout_buffer != NULL && buffer_length > 0) {
        stdout_buffer[0] = '\0';
    }

    if (stdout_buffer == NULL || buffer_length == 0) {
        char scratch[256];
        while (fgets(scratch, sizeof(scratch), stream) != NULL) {
            (void)scratch;
        }
        if (written != NULL) {
            *written = 0;
        }
        return TRACER_DOCS_STATUS_OK;
    }

    while (fgets(stdout_buffer + total, (int)(buffer_length - total), stream) != NULL) {
        size_t chunk = strlen(stdout_buffer + total);
        total += chunk;
        if (total + 1 >= buffer_length) {
            stdout_buffer[buffer_length - 1] = '\0';
            if (written != NULL) {
                *written = buffer_length - 1;
            }
            return TRACER_DOCS_STATUS_IO_ERROR;
        }
    }

    if (written != NULL) {
        *written = total;
    }
    return TRACER_DOCS_STATUS_OK;
}

static tracer_docs_status_t run_command_capture(
    const char *command,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written
) {
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    tracer_docs_status_t status = append_command_output(pipe, stdout_buffer, buffer_length, written);
    int exit_status = pclose(pipe);
    // pclose returns -1 on error, or the exit status in wait(2) format
    // On macOS, occasionally pclose might return -1 with ECHILD if the child was already reaped
    // This can happen in test environments, so we only treat it as error if we didn't get output
    if (exit_status == -1) {
        // If we got valid output, ignore the pclose error (likely ECHILD)
        if (status == TRACER_DOCS_STATUS_OK && written != NULL && *written > 0) {
            // Ignore the error - we got the output successfully
        } else {
            status = TRACER_DOCS_STATUS_IO_ERROR;
        }
    } else if (exit_status != 0) {
        // Non-zero exit status indicates command failure
        status = TRACER_DOCS_STATUS_IO_ERROR;
    }
    return status;
}

static tracer_docs_status_t compile_c_example(const char *source_path, char *output_path, size_t output_path_len) {
    char source_directory[PATH_MAX];
    if (snprintf(source_directory, sizeof(source_directory), "%s", source_path) < 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    char *slash = strrchr(source_directory, '/');
    if (slash == NULL) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }
    *slash = '\0';

    char template_path[PATH_MAX];
    if (snprintf(template_path, sizeof(template_path), "%s/example-XXXXXX", source_directory) < 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    char *temp_dir = mkdtemp(template_path);
    if (temp_dir == NULL) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    char binary_path[PATH_MAX];
    if (snprintf(binary_path, sizeof(binary_path), "%s/example.out", temp_dir) < 0) {
        (void)rmdir(temp_dir);
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    char command[PATH_MAX * 2];
    if (snprintf(
            command,
            sizeof(command),
            "cc -std=c11 -O0 -Wall -Wextra -pedantic -o '%s' '%s' 2>&1",
            binary_path,
            source_path
        ) < 0) {
        (void)unlink(binary_path);
        (void)rmdir(temp_dir);
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    char compile_output[256];
    tracer_docs_status_t status = run_command_capture(command, compile_output, sizeof(compile_output), NULL);
    if (status != TRACER_DOCS_STATUS_OK) {
        (void)unlink(binary_path);
        (void)rmdir(temp_dir);
        return status;
    }

    if (snprintf(output_path, output_path_len, "%s", binary_path) < 0) {
        (void)unlink(binary_path);
        (void)rmdir(temp_dir);
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    return TRACER_DOCS_STATUS_OK;
}

static void cleanup_binary(const char *path) {
    if (path == NULL) {
        return;
    }

    char directory[PATH_MAX];
    if (snprintf(directory, sizeof(directory), "%s", path) < 0) {
        return;
    }

    char *slash = strrchr(directory, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';

    (void)unlink(path);
    (void)rmdir(directory);
}

static tracer_docs_status_t execute_binary(
    const char *binary_path,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written
) {
    char command[PATH_MAX * 2];
    if (snprintf(command, sizeof(command), "'%s'", binary_path) < 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }
    return run_command_capture(command, stdout_buffer, buffer_length, written);
}

static tracer_docs_status_t execute_shell_script(
    const char *source_path,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written
) {
    char command[PATH_MAX * 2];
    if (snprintf(command, sizeof(command), "/bin/sh '%s'", source_path) < 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }
    return run_command_capture(command, stdout_buffer, buffer_length, written);
}

static int has_extension(const char *path, const char *extension) {
    if (path == NULL || extension == NULL) {
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return 0;
    }
    return strcmp(dot, extension) == 0;
}

tracer_example_runner_t *tracer_example_runner_create(void) {
    tracer_example_runner_t *runner = (tracer_example_runner_t *)calloc(1, sizeof(tracer_example_runner_t));
    if (runner == NULL) {
        return NULL;
    }
    atomic_init(&runner->active_sessions, 0);
    atomic_init(&runner->last_duration_ns, 0);
    atomic_init(&runner->last_exec_duration_ns, 0);
    return runner;
}

void tracer_example_runner_destroy(tracer_example_runner_t *runner) {
    if (runner == NULL) {
        return;
    }
    free(runner);
}

tracer_docs_status_t tracer_example_runner_execute(
    tracer_example_runner_t *runner,
    const char *source_path,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written,
    tracer_example_result_t *result
) {
    if (runner == NULL || source_path == NULL) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    struct timespec start = {0};
    struct timespec end = {0};
    struct timespec exec_start = {0};
    struct timespec exec_end = {0};
    uint64_t exec_duration = 0;
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    atomic_fetch_add_explicit(&runner->active_sessions, 1, memory_order_acq_rel);

    tracer_docs_status_t status = TRACER_DOCS_STATUS_UNSUPPORTED;

    if (has_extension(source_path, ".c")) {
        char binary_path[PATH_MAX];
        int compiled = 0;
        status = compile_c_example(source_path, binary_path, sizeof(binary_path));
        if (status == TRACER_DOCS_STATUS_OK) {
            compiled = 1;
            // Time only the execution phase
            (void)clock_gettime(CLOCK_MONOTONIC, &exec_start);
            tracer_docs_status_t exec_status = execute_binary(
                binary_path,
                stdout_buffer,
                buffer_length,
                written
            );
            (void)clock_gettime(CLOCK_MONOTONIC, &exec_end);
            exec_duration = elapsed_ns(&exec_start, &exec_end);
            if (exec_status != TRACER_DOCS_STATUS_OK) {
                status = exec_status;
            }
        }
        if (compiled != 0) {
            cleanup_binary(binary_path);
        }
    } else if (has_extension(source_path, ".sh")) {
        // For shell scripts, the entire operation is execution
        (void)clock_gettime(CLOCK_MONOTONIC, &exec_start);
        status = execute_shell_script(source_path, stdout_buffer, buffer_length, written);
        (void)clock_gettime(CLOCK_MONOTONIC, &exec_end);
        exec_duration = elapsed_ns(&exec_start, &exec_end);
    }

    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    const uint64_t duration = elapsed_ns(&start, &end);
    atomic_store_explicit(&runner->last_duration_ns, duration, memory_order_release);
    atomic_store_explicit(&runner->last_exec_duration_ns, exec_duration, memory_order_release);

    if (result != NULL) {
        // For C programs, report execution time only in the result
        // For shell scripts, report total time (since there's no compilation)
        if (has_extension(source_path, ".c") && exec_duration > 0) {
            result->duration_ns = exec_duration;
        } else {
            result->duration_ns = duration;
        }
        result->stdout_matched = 0;
        if (written != NULL) {
            result->stdout_size = *written;
        } else if (stdout_buffer != NULL) {
            result->stdout_size = strlen(stdout_buffer);
        } else {
            result->stdout_size = 0;
        }
    }

    // Apply budget check only to the execution phase duration (when we actually executed something)
    if (exec_duration > 0 && exec_duration > TRACER_EXAMPLE_EXECUTION_BUDGET_NS && status == TRACER_DOCS_STATUS_OK) {
        status = TRACER_DOCS_STATUS_IO_ERROR;
    }

    atomic_fetch_sub_explicit(&runner->active_sessions, 1, memory_order_acq_rel);
    return status;
}

tracer_docs_status_t tracer_example_runner_execute_and_verify(
    tracer_example_runner_t *runner,
    const char *source_path,
    const char *expected_substring,
    char *stdout_buffer,
    size_t buffer_length,
    tracer_example_result_t *result
) {
    size_t written = 0;
    tracer_docs_status_t status = tracer_example_runner_execute(
        runner,
        source_path,
        stdout_buffer,
        buffer_length,
        &written,
        result
    );

    if (status != TRACER_DOCS_STATUS_OK) {
        return status;
    }

    int matched = 0;
    if (expected_substring != NULL && stdout_buffer != NULL) {
        matched = strstr(stdout_buffer, expected_substring) != NULL;
    }

    if (result != NULL) {
        result->stdout_matched = matched;
        result->stdout_size = written;
    }

    return matched ? TRACER_DOCS_STATUS_OK : TRACER_DOCS_STATUS_IO_ERROR;
}

uint64_t tracer_example_runner_get_last_duration_ns(const tracer_example_runner_t *runner) {
    if (runner == NULL) {
        return 0;
    }
    return atomic_load_explicit(&runner->last_duration_ns, memory_order_acquire);
}

unsigned int tracer_example_runner_active_sessions(const tracer_example_runner_t *runner) {
    if (runner == NULL) {
        return 0;
    }
    return atomic_load_explicit(&runner->active_sessions, memory_order_acquire);
}
