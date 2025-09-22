#include "tracer_backend/docs/platform_check.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int check_executable_present(const char *path) {
    if (path == NULL) {
        return 0;
    }
    return access(path, X_OK) == 0;
}

void tracer_platform_snapshot(tracer_platform_status_t *status) {
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));

#if defined(__APPLE__)
    status->is_macos = 1;
#elif defined(__linux__)
    status->is_linux = 1;
#else
    status->is_macos = 0;
    status->is_linux = 0;
#endif

    if (status->is_macos) {
        status->codesign_tool_available = check_executable_present("/usr/bin/codesign");
    }

    if (status->is_linux) {
        const char *candidates[] = {
            "/usr/sbin/setcap",
            "/sbin/setcap",
            "/usr/bin/setcap"
        };
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
            if (check_executable_present(candidates[i])) {
                status->linux_capabilities_available = 1;
                break;
            }
        }
    }
}

tracer_docs_status_t tracer_platform_render_summary(
    const tracer_platform_status_t *status,
    char *buffer,
    size_t buffer_length,
    size_t *written
) {
    if (status == NULL || buffer == NULL || buffer_length == 0) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    int count = snprintf(
        buffer,
        buffer_length,
        "## Platform Summary\n"
        "- macOS detected: %s (codesign %s)\n"
        "- Linux detected: %s (capabilities %s)\n\n",
        status->is_macos ? "yes" : "no",
        status->codesign_tool_available ? "available" : "missing",
        status->is_linux ? "yes" : "no",
        status->linux_capabilities_available ? "available" : "missing"
    );

    if (count < 0 || (size_t)count >= buffer_length) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    if (written != NULL) {
        *written = (size_t)count;
    }
    return TRACER_DOCS_STATUS_OK;
}

int tracer_platform_codesign_enforced(void) {
#if defined(__APPLE__)
    return 1;
#else
    return 0;
#endif
}

int tracer_platform_capabilities_required(void) {
#if defined(__linux__)
    return 1;
#else
    return 0;
#endif
}
