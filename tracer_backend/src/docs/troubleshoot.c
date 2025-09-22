#include "tracer_backend/docs/troubleshoot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracer_backend/docs/platform_check.h"

tracer_docs_status_t tracer_troubleshoot_generate_report(
    tracer_troubleshoot_report_t *report
) {
    if (report == NULL) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    memset(report, 0, sizeof(*report));

    tracer_platform_status_t status = {0};
    tracer_platform_snapshot(&status);

    report->platform_supported = status.is_macos || status.is_linux;
    report->requires_codesign = status.is_macos && !status.codesign_tool_available;
    report->requires_linux_capabilities = status.is_linux && !status.linux_capabilities_available;

    const char *force_codesign = getenv("ADA_DOCS_FORCE_CODESIGN_MISSING");
    if (force_codesign != NULL && force_codesign[0] == '1') {
        report->requires_codesign = 1;
    }

    const char *force_capabilities = getenv("ADA_DOCS_FORCE_CAPABILITIES_MISSING");
    if (force_capabilities != NULL && force_capabilities[0] == '1') {
        report->requires_linux_capabilities = 1;
    }

    if (!report->platform_supported) {
        (void)snprintf(
            report->actionable_steps,
            sizeof(report->actionable_steps),
            "Unsupported platform. Please provision macOS or Linux runtime."
        );
        return TRACER_DOCS_STATUS_OK;
    }

    char buffer[256];
    buffer[0] = '\0';

    if (report->requires_codesign) {
        strncat(buffer, "Run 'xcode-select --install' then retry codesign setup. ", sizeof(buffer) - strlen(buffer) - 1);
    }

    if (report->requires_linux_capabilities) {
        strncat(buffer, "Install libcap and ensure setcap binary is available. ", sizeof(buffer) - strlen(buffer) - 1);
    }

    if (buffer[0] == '\0') {
        strncat(buffer, "Platform ready — no blocking issues detected. ", sizeof(buffer) - 1);
    }

    strncat(buffer, "Validate tracing by running example runner smoke tests.", sizeof(buffer) - strlen(buffer) - 1);

    (void)snprintf(
        report->actionable_steps,
        sizeof(report->actionable_steps),
        "%s",
        buffer
    );

    return TRACER_DOCS_STATUS_OK;
}

tracer_docs_status_t tracer_troubleshoot_render_report(
    const tracer_troubleshoot_report_t *report,
    char *buffer,
    size_t buffer_length,
    size_t *written
) {
    if (report == NULL || buffer == NULL || buffer_length == 0) {
        return TRACER_DOCS_STATUS_INVALID_ARGUMENT;
    }

    int count = snprintf(
        buffer,
        buffer_length,
        "## Troubleshooting\n"
        "- Platform supported: %s\n"
        "- Codesign required: %s\n"
        "- Linux capabilities required: %s\n"
        "- Actionable steps: %s\n\n",
        report->platform_supported ? "yes" : "no",
        report->requires_codesign ? "yes" : "no",
        report->requires_linux_capabilities ? "yes" : "no",
        report->actionable_steps
    );

    if (count < 0 || (size_t)count >= buffer_length) {
        return TRACER_DOCS_STATUS_IO_ERROR;
    }

    if (written != NULL) {
        *written = (size_t)count;
    }
    return TRACER_DOCS_STATUS_OK;
}
