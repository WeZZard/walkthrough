#ifndef ATF_V4_WRITER_TEST_HELPERS_H
#define ATF_V4_WRITER_TEST_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#include <tracer_backend/atf/atf_v4_writer.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "trace_schema.pb-c.h"

typedef struct {
    Google__Protobuf__Timestamp timestamp;
    TraceStart trace_start;
    const char* trace_args_storage[ATF_V4_MAX_ARGS];
    TraceEnd trace_end;
    FunctionCall function_call;
    FunctionCall__ArgumentRegistersEntry call_entries[ATF_V4_MAX_REGISTERS];
    FunctionReturn function_return;
    FunctionReturn__ReturnRegistersEntry return_entries[ATF_V4_MAX_REGISTERS];
    SignalDelivery signal_delivery;
    SignalDelivery__RegistersEntry signal_entries[ATF_V4_MAX_REGISTERS];
} AtfV4ProtoScratchTest;

void atf_v4_test_reset_scratch(AtfV4ProtoScratchTest* scratch);

int atf_v4_test_convert_function_return(const AtfV4FunctionReturn* src, AtfV4ProtoScratchTest* scratch);
int atf_v4_test_convert_signal_delivery(const AtfV4SignalDelivery* src, AtfV4ProtoScratchTest* scratch);
int atf_v4_test_convert_event(const AtfV4Event* src, Event* dst, AtfV4ProtoScratchTest* scratch);
int atf_v4_test_convert_trace_start(const AtfV4TraceStart* src, AtfV4ProtoScratchTest* scratch);
int atf_v4_test_convert_function_call(const AtfV4FunctionCall* src, AtfV4ProtoScratchTest* scratch);

int atf_v4_test_ensure_directory(const char* path);
int atf_v4_test_flush(AtfV4Writer* writer);

size_t atf_v4_test_encode_varint(uint64_t value, uint8_t* out);

int atf_v4_test_append_event(AtfV4Writer* writer, const Event* proto);
int atf_v4_test_write_all(int fd, const uint8_t* data, size_t len);
int atf_v4_test_write_manifest(const AtfV4Writer* writer);

void atf_v4_test_force_malloc_failure_once(void);
void atf_v4_test_force_write_failure_once(void);
void atf_v4_test_force_fsync_failure_once(void);
void atf_v4_test_force_rename_failure_once(void);
void atf_v4_test_clear_io_faults(void);

#ifdef __cplusplus
}
#endif

#endif  // ATF_V4_WRITER_TEST_HELPERS_H
