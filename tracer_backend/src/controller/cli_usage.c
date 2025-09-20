#include <tracer_backend/controller/cli_usage.h>

#include <stdio.h>

size_t tracer_controller_format_usage(char* buffer, size_t buffer_size, const char* program) {
    if (buffer == NULL || buffer_size == 0 || program == NULL) {
        if (buffer != NULL && buffer_size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "Usage: %s <mode> <target> [options]\n"
        "\nModes:\n"
        "  spawn    - Spawn and trace a new process\n"
        "  attach   - Attach to an existing process\n"
        "\nExamples:\n"
        "  %s spawn ./test_cli --wait\n"
        "  %s spawn ./test_runloop\n"
        "  %s attach 1234\n"
        "\nOptions:\n"
        "  --output <dir>    - Output directory for traces (default: ./traces)\n"
        "  --exclude <csv>   - Comma/semicolon-separated list of symbols to exclude from hooks\n"
        "  --duration <sec>  - Automatically stop tracing after the given duration in seconds\n",
        program,
        program,
        program,
        program
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    if ((size_t)written >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
    }

    return (size_t)written;
}
