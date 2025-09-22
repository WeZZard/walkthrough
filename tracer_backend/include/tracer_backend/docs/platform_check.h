#ifndef TRACER_BACKEND_DOCS_PLATFORM_CHECK_H
#define TRACER_BACKEND_DOCS_PLATFORM_CHECK_H

#include <stddef.h>

#include "tracer_backend/docs/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tracer_platform_status {
    int is_macos;
    int is_linux;
    int codesign_tool_available;
    int linux_capabilities_available;
} tracer_platform_status_t;

void tracer_platform_snapshot(tracer_platform_status_t *status);
tracer_docs_status_t tracer_platform_render_summary(
    const tracer_platform_status_t *status,
    char *buffer,
    size_t buffer_length,
    size_t *written
);

int tracer_platform_codesign_enforced(void);
int tracer_platform_capabilities_required(void);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_DOCS_PLATFORM_CHECK_H
