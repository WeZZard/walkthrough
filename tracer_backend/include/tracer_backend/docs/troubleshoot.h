#ifndef TRACER_BACKEND_DOCS_TROUBLESHOOT_H
#define TRACER_BACKEND_DOCS_TROUBLESHOOT_H

#include <stddef.h>

#include "tracer_backend/docs/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tracer_troubleshoot_report {
    int requires_codesign;
    int requires_linux_capabilities;
    int platform_supported;
    char actionable_steps[256];
} tracer_troubleshoot_report_t;

tracer_docs_status_t tracer_troubleshoot_generate_report(
    tracer_troubleshoot_report_t *report
);

tracer_docs_status_t tracer_troubleshoot_render_report(
    const tracer_troubleshoot_report_t *report,
    char *buffer,
    size_t buffer_length,
    size_t *written
);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_DOCS_TROUBLESHOOT_H
