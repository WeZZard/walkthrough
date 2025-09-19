#ifndef TRACER_BACKEND_ATF_V4_WRITER_H
#define TRACER_BACKEND_ATF_V4_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ATF_V4_MAX_REGISTERS
#define ATF_V4_MAX_REGISTERS 16
#endif

#ifndef ATF_V4_MAX_MODULES
#define ATF_V4_MAX_MODULES 64
#endif

#ifndef ATF_V4_UUID_STRING_SIZE
#define ATF_V4_UUID_STRING_SIZE 37
#endif

#ifndef ATF_V4_MAX_STACK_BYTES
#define ATF_V4_MAX_STACK_BYTES 256
#endif

#ifndef ATF_V4_MAX_ARGS
#define ATF_V4_MAX_ARGS 16
#endif

// Forward declaration for protobuf structs
typedef struct _Event Event;
typedef struct _TraceStart TraceStart;
typedef struct _TraceEnd TraceEnd;
typedef struct _FunctionCall FunctionCall;
typedef struct _FunctionReturn FunctionReturn;
typedef struct _SignalDelivery SignalDelivery;
typedef struct _Google__Protobuf__Timestamp Google__Protobuf__Timestamp;

typedef enum {
    ATF_V4_EVENT_TRACE_START = 0,
    ATF_V4_EVENT_TRACE_END   = 1,
    ATF_V4_EVENT_FUNCTION_CALL = 2,
    ATF_V4_EVENT_FUNCTION_RETURN = 3,
    ATF_V4_EVENT_SIGNAL_DELIVERY = 4
} AtfV4EventKind;

typedef struct {
    char     name[32];
    uint64_t value;
} AtfV4RegisterEntry;

typedef struct {
    const char* symbol;
    uint64_t address;
    AtfV4RegisterEntry registers[ATF_V4_MAX_REGISTERS];
    size_t register_count;
    const uint8_t* stack_bytes;
    size_t stack_size;
} AtfV4FunctionCall;

typedef struct {
    const char* symbol;
    uint64_t address;
    AtfV4RegisterEntry registers[ATF_V4_MAX_REGISTERS];
    size_t register_count;
} AtfV4FunctionReturn;

typedef struct {
    int32_t number;
    const char* name;
    AtfV4RegisterEntry registers[ATF_V4_MAX_REGISTERS];
    size_t register_count;
} AtfV4SignalDelivery;

typedef struct {
    const char* executable_path;
    const char* const* argv;
    size_t argc;
    const char* operating_system;
    const char* cpu_architecture;
} AtfV4TraceStart;

typedef struct {
    int32_t exit_code;
} AtfV4TraceEnd;

typedef struct {
    AtfV4EventKind kind;
    uint64_t event_id;
    int32_t thread_id;
    uint64_t timestamp_ns;
    union {
        AtfV4TraceStart trace_start;
        AtfV4TraceEnd trace_end;
        AtfV4FunctionCall function_call;
        AtfV4FunctionReturn function_return;
        AtfV4SignalDelivery signal_delivery;
    } payload;
} AtfV4Event;

typedef struct {
    const char* output_root;   // Base directory (e.g., "/tmp")
    const char* session_label; // Optional session label override
    uint32_t pid;
    uint64_t session_id;
    bool enable_manifest;
} AtfV4WriterConfig;

typedef struct AtfV4Writer {
    char base_path[4096];
    char session_dir[4096];
    char events_path[4096];
    char manifest_path[4096];
    char manifest_os[32];
    char manifest_arch[32];

    atomic_uint_fast64_t event_count;
    atomic_uint_fast64_t bytes_written;
    atomic_uint_fast32_t write_errors;

    uint64_t trace_start_ns;
    uint64_t trace_end_ns;

    int events_fd;
    FILE* manifest_fp;

    char modules[ATF_V4_MAX_MODULES][ATF_V4_UUID_STRING_SIZE];
    atomic_uint_fast32_t module_count;

    atomic_uint_fast64_t next_event_id;

    bool initialized;
    bool finalized;
    bool manifest_enabled;

    // Cached config
    uint32_t pid;
    uint64_t session_id;
} AtfV4Writer;

int atf_v4_writer_init(AtfV4Writer* writer, const AtfV4WriterConfig* config);
void atf_v4_writer_deinit(AtfV4Writer* writer);

int atf_v4_writer_write_event(AtfV4Writer* writer, const AtfV4Event* event);
int atf_v4_writer_flush(AtfV4Writer* writer);
int atf_v4_writer_finalize(AtfV4Writer* writer);

uint64_t atf_v4_writer_event_count(const AtfV4Writer* writer);
uint64_t atf_v4_writer_bytes_written(const AtfV4Writer* writer);
uint32_t atf_v4_writer_module_count(const AtfV4Writer* writer);
const char* atf_v4_writer_session_dir(const AtfV4Writer* writer);
const char* atf_v4_writer_events_path(const AtfV4Writer* writer);
const char* atf_v4_writer_manifest_path(const AtfV4Writer* writer);
uint32_t atf_v4_writer_write_errors(const AtfV4Writer* writer);

int atf_v4_writer_register_module(AtfV4Writer* writer, const char* module_uuid);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_ATF_V4_WRITER_H
