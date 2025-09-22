#ifndef TRACER_BACKEND_DOCS_DOC_BUILDER_H
#define TRACER_BACKEND_DOCS_DOC_BUILDER_H

#include <stddef.h>

#include "tracer_backend/docs/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Hard budgets derived from tech design (nanoseconds)
#define TRACER_DOC_GENERATION_BUDGET_NS UINT64_C(100000000)

// Forward declaration of opaque builder state
typedef struct tracer_doc_builder tracer_doc_builder_t;

tracer_doc_builder_t *tracer_doc_builder_create(void);
void tracer_doc_builder_destroy(tracer_doc_builder_t *builder);

tracer_docs_status_t tracer_doc_builder_generate_getting_started(
    tracer_doc_builder_t *builder,
    const char *workspace_root,
    char *buffer,
    size_t buffer_length,
    size_t *written
);

tracer_docs_status_t tracer_doc_builder_generate_quick_reference(
    tracer_doc_builder_t *builder,
    char *buffer,
    size_t buffer_length,
    size_t *written
);

uint64_t tracer_doc_builder_get_last_duration_ns(const tracer_doc_builder_t *builder);
unsigned int tracer_doc_builder_active_sessions(const tracer_doc_builder_t *builder);
void tracer_doc_builder_reset_metrics(tracer_doc_builder_t *builder);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_DOCS_DOC_BUILDER_H
