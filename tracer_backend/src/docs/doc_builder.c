#include "tracer_backend/docs/doc_builder.h"

#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tracer_backend/docs/platform_check.h"
#include "tracer_backend/docs/troubleshoot.h"

struct tracer_doc_builder {
    atomic_flag guard;
    atomic_uint active_sessions;
    atomic_uint_fast64_t last_duration_ns;
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

static tracer_docs_status_t append_format(
    char **cursor,
    size_t *remaining,
    size_t *total_written,
    const char *fmt,
    ...
) {
    if (*remaining == 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(*cursor, *remaining, fmt, args);
    va_end(args);

    if (written < 0) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    if ((size_t)written >= *remaining) {
        *total_written += *remaining - 1;
        *cursor += *remaining - 1;
        *remaining = 1;
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    *cursor += written;
    *remaining -= (size_t)written;
    *total_written += (size_t)written;
    return TRACER_DOCS_STATUS_OK;
}

static tracer_docs_status_t append_quick_reference(
    char **cursor,
    size_t *remaining,
    size_t *total_written
) {
    const char *quick_reference =
        "## Quick Reference\n"
        "### Command Reference\n"
        "- `cargo build --release` — Build all components with optimisations.\n"
        "- `cargo test --all` — Execute the full backend validation suite.\n"
        "- `maturin develop -m query_engine/Cargo.toml` — Build Python bindings locally.\n\n"
        "### Pattern Library\n"
        "- Initialization pattern: `tracer_doc_builder_generate_getting_started()` followed by `tracer_example_runner_execute_and_verify()`\n"
        "- Concurrency pattern: Use atomic guards to coordinate documentation writes.\n"
        "- Validation pattern: Render troubleshoot report after every generation cycle.\n\n"
        "### Environment Variables\n"
        "- `ADA_WORKSPACE_ROOT` — Absolute workspace path auto-injected by Cargo.\n"
        "- `ADA_BUILD_PROFILE` — Tracks debug vs release pipelines.\n"
        "- `ADA_ENABLE_THREAD_SANITIZER` / `ADA_ENABLE_ADDRESS_SANITIZER` — Opt-in instrumentation knobs.\n\n";

    return append_format(cursor, remaining, total_written, "%s", quick_reference);
}

tracer_doc_builder_t *tracer_doc_builder_create(void) {
    tracer_doc_builder_t *builder = (tracer_doc_builder_t *)calloc(1, sizeof(tracer_doc_builder_t));
    if (builder == NULL) {
        return NULL;
    }

    atomic_flag_clear(&builder->guard);
    atomic_init(&builder->active_sessions, 0);
    atomic_init(&builder->last_duration_ns, 0);
    return builder;
}

void tracer_doc_builder_destroy(tracer_doc_builder_t *builder) {
    if (builder == NULL) {
        return;
    }
    free(builder);
}

static tracer_docs_status_t acquire_builder(tracer_doc_builder_t *builder) {
    if (atomic_flag_test_and_set_explicit(&builder->guard, memory_order_acquire)) {
        return TRACER_DOCS_STATUS_BUSY;
    }
    atomic_fetch_add_explicit(&builder->active_sessions, 1, memory_order_acq_rel);
    return TRACER_DOCS_STATUS_OK;
}

static void release_builder(tracer_doc_builder_t *builder) {
    atomic_fetch_sub_explicit(&builder->active_sessions, 1, memory_order_acq_rel);
    atomic_flag_clear_explicit(&builder->guard, memory_order_release);
}

tracer_docs_status_t tracer_doc_builder_generate_getting_started(
    tracer_doc_builder_t *builder,
    const char *workspace_root,
    char *buffer,
    size_t buffer_length,
    size_t *written
) {
    if (builder == NULL || buffer == NULL || buffer_length == 0) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    struct timespec start = {0};
    struct timespec end = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    tracer_docs_status_t status = acquire_builder(builder);
    if (status != TRACER_DOCS_STATUS_OK) {
        return status;
    }

    char *cursor = buffer;
    size_t remaining = buffer_length;
    size_t total_written = 0;

    status = append_format(
        &cursor,
        &remaining,
        &total_written,
        "# ADA Tracer Backend — Getting Started\n\n"
        "Welcome to the Getting Started Guide. All artefacts are generated in <workspace> %s.\n\n",
        workspace_root != NULL ? workspace_root : "<unknown>"
    );
    if (status != TRACER_DOCS_STATUS_OK) {
        goto done;
    }

    tracer_platform_status_t platform = {0};
    tracer_platform_snapshot(&platform);

    status = append_format(
        &cursor,
        &remaining,
        &total_written,
        "## Platform Checklist\n"
        "- macOS: %s (codesign tool %s).\n"
        "- Linux: %s (setcap %s).\n\n",
        platform.is_macos ? "detected" : "not active",
        platform.codesign_tool_available ? "available" : "missing",
        platform.is_linux ? "detected" : "not active",
        platform.linux_capabilities_available ? "available" : "missing"
    );
    if (status != TRACER_DOCS_STATUS_OK) {
        goto done;
    }

    tracer_troubleshoot_report_t report = {0};
    status = tracer_troubleshoot_generate_report(&report);
    if (status != TRACER_DOCS_STATUS_OK) {
        goto done;
    }

    size_t troubleshooting_written = 0;
    status = tracer_troubleshoot_render_report(
        &report,
        cursor,
        remaining,
        &troubleshooting_written
    );
    if (status != TRACER_DOCS_STATUS_OK) {
        goto done;
    }

    cursor += troubleshooting_written;
    remaining -= troubleshooting_written;
    total_written += troubleshooting_written;

    status = append_quick_reference(&cursor, &remaining, &total_written);

    if (status == TRACER_DOCS_STATUS_OK) {
        status = append_format(
            &cursor,
            &remaining,
            &total_written,
            "## Example Workflow\n"
            "1. Author examples in \"examples/basic\" or siblings.\n"
            "2. Use tracer_example_runner_execute_and_verify() to compile and run.\n"
            "3. Capture troubleshooting insights at the end of the session.\n"
        );
    }

    if (status == TRACER_DOCS_STATUS_OK && written != NULL) {
        *written = total_written;
    }

    if (remaining > 0) {
        *cursor = '\0';
    }

done:
    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    const uint64_t duration = elapsed_ns(&start, &end);
    atomic_store_explicit(&builder->last_duration_ns, duration, memory_order_release);

    if (duration > TRACER_DOC_GENERATION_BUDGET_NS && status == TRACER_DOCS_STATUS_OK) {
        status = TRACER_DOCS_STATUS_IO_ERROR;
    }

    release_builder(builder);
    return status;
}

tracer_docs_status_t tracer_doc_builder_generate_quick_reference(
    tracer_doc_builder_t *builder,
    char *buffer,
    size_t buffer_length,
    size_t *written
) {
    if (builder == NULL || buffer == NULL || buffer_length == 0) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    struct timespec start = {0};
    struct timespec end = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    tracer_docs_status_t status = acquire_builder(builder);
    if (status != TRACER_DOCS_STATUS_OK) {
        return status;
    }

    char *cursor = buffer;
    size_t remaining = buffer_length;
    size_t total_written = 0;

    status = append_quick_reference(&cursor, &remaining, &total_written);
    if (status == TRACER_DOCS_STATUS_OK && written != NULL) {
        *written = total_written;
    }
    if (remaining > 0) {
        *cursor = '\0';
    }

    (void)clock_gettime(CLOCK_MONOTONIC, &end);
    const uint64_t duration = elapsed_ns(&start, &end);
    atomic_store_explicit(&builder->last_duration_ns, duration, memory_order_release);

    release_builder(builder);
    return status;
}

uint64_t tracer_doc_builder_get_last_duration_ns(const tracer_doc_builder_t *builder) {
    if (builder == NULL) {
        return 0;
    }
    return atomic_load_explicit(&builder->last_duration_ns, memory_order_acquire);
}

unsigned int tracer_doc_builder_active_sessions(const tracer_doc_builder_t *builder) {
    if (builder == NULL) {
        return 0;
    }
    return atomic_load_explicit(&builder->active_sessions, memory_order_acquire);
}

void tracer_doc_builder_reset_metrics(tracer_doc_builder_t *builder) {
    if (builder == NULL) {
        return;
    }
    atomic_store_explicit(&builder->last_duration_ns, 0, memory_order_release);
}
