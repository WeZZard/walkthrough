#ifndef TRACER_BACKEND_DOCS_EXAMPLE_RUNNER_H
#define TRACER_BACKEND_DOCS_EXAMPLE_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#include "tracer_backend/docs/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tracer_example_runner tracer_example_runner_t;

typedef struct tracer_example_result {
    uint64_t duration_ns;
    int stdout_matched;
    size_t stdout_size;
} tracer_example_result_t;

tracer_example_runner_t *tracer_example_runner_create(void);
void tracer_example_runner_destroy(tracer_example_runner_t *runner);

tracer_docs_status_t tracer_example_runner_execute(
    tracer_example_runner_t *runner,
    const char *source_path,
    char *stdout_buffer,
    size_t buffer_length,
    size_t *written,
    tracer_example_result_t *result
);

tracer_docs_status_t tracer_example_runner_execute_and_verify(
    tracer_example_runner_t *runner,
    const char *source_path,
    const char *expected_substring,
    char *stdout_buffer,
    size_t buffer_length,
    tracer_example_result_t *result
);

uint64_t tracer_example_runner_get_last_duration_ns(const tracer_example_runner_t *runner);
unsigned int tracer_example_runner_active_sessions(const tracer_example_runner_t *runner);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_DOCS_EXAMPLE_RUNNER_H
