#ifndef TRACER_BACKEND_CONTROLLER_CLI_USAGE_H
#define TRACER_BACKEND_CONTROLLER_CLI_USAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format the tracer controller usage message into the provided buffer.
 *
 * @param buffer Destination buffer that receives the formatted usage text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @param program Program name to embed within the usage message.
 * @return Number of characters that would have been written (excluding nul terminator).
 */
size_t tracer_controller_format_usage(char* buffer, size_t buffer_size, const char* program);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_CONTROLLER_CLI_USAGE_H
