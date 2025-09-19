#ifndef TRACE_SCHEMA_PB_C_H
#define TRACE_SCHEMA_PB_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Google__Protobuf__Timestamp {
    uint64_t seconds;
    uint32_t nanos;
} Google__Protobuf__Timestamp;

#define GOOGLE__PROTOBUF__TIMESTAMP__INIT {0, 0}

typedef struct _TraceStart {
    const char* executable_path;
    char** args;
    size_t n_args;
    const char* operating_system;
    const char* cpu_architecture;
} TraceStart;

#define TRACE_START__INIT {NULL, NULL, 0, NULL, NULL}

typedef struct _TraceEnd {
    int32_t exit_code;
} TraceEnd;

#define TRACE_END__INIT {0}

typedef struct _FunctionCall__ArgumentRegistersEntry {
    char* key;
    uint64_t value;
} FunctionCall__ArgumentRegistersEntry;

#define FUNCTION_CALL__ARGUMENT_REGISTERS_ENTRY__INIT {NULL, 0}

typedef struct _FunctionCall {
    const char* symbol;
    uint64_t address;
    FunctionCall__ArgumentRegistersEntry* argument_registers;
    size_t n_argument_registers;
    struct {
        size_t len;
        uint8_t* data;
    } stack_shallow_copy;
} FunctionCall;

#define FUNCTION_CALL__INIT {NULL, 0, NULL, 0, {0, NULL}}

typedef struct _FunctionReturn__ReturnRegistersEntry {
    char* key;
    uint64_t value;
} FunctionReturn__ReturnRegistersEntry;

#define FUNCTION_RETURN__RETURN_REGISTERS_ENTRY__INIT {NULL, 0}

typedef struct _FunctionReturn {
    const char* symbol;
    uint64_t address;
    FunctionReturn__ReturnRegistersEntry* return_registers;
    size_t n_return_registers;
} FunctionReturn;

#define FUNCTION_RETURN__INIT {NULL, 0, NULL, 0}

typedef struct _SignalDelivery__RegistersEntry {
    char* key;
    uint64_t value;
} SignalDelivery__RegistersEntry;

#define SIGNAL_DELIVERY__REGISTERS_ENTRY__INIT {NULL, 0}

typedef struct _SignalDelivery {
    int32_t number;
    const char* name;
    SignalDelivery__RegistersEntry* registers;
    size_t n_registers;
} SignalDelivery;

#define SIGNAL_DELIVERY__INIT {0, NULL, NULL, 0}

typedef enum {
    EVENT__PAYLOAD__NOT_SET = 0,
    EVENT__PAYLOAD_TRACE_START = 10,
    EVENT__PAYLOAD_TRACE_END = 11,
    EVENT__PAYLOAD_FUNCTION_CALL = 12,
    EVENT__PAYLOAD_FUNCTION_RETURN = 13,
    EVENT__PAYLOAD_SIGNAL_DELIVERY = 14
} Event__PayloadCase;

typedef struct _Event {
    uint64_t event_id;
    int32_t thread_id;
    Google__Protobuf__Timestamp* timestamp;
    Event__PayloadCase payload_case;
    TraceStart* trace_start;
    TraceEnd* trace_end;
    FunctionCall* function_call;
    FunctionReturn* function_return;
    SignalDelivery* signal_delivery;
} Event;

#define EVENT__INIT {0, 0, NULL, EVENT__PAYLOAD__NOT_SET, NULL, NULL, NULL, NULL, NULL}

size_t trace_start__get_packed_size(const TraceStart* message);
size_t trace_start__pack(const TraceStart* message, uint8_t* out);

size_t trace_end__get_packed_size(const TraceEnd* message);
size_t trace_end__pack(const TraceEnd* message, uint8_t* out);

size_t function_call__get_packed_size(const FunctionCall* message);
size_t function_call__pack(const FunctionCall* message, uint8_t* out);

size_t function_return__get_packed_size(const FunctionReturn* message);
size_t function_return__pack(const FunctionReturn* message, uint8_t* out);

size_t signal_delivery__get_packed_size(const SignalDelivery* message);
size_t signal_delivery__pack(const SignalDelivery* message, uint8_t* out);

size_t event__get_packed_size(const Event* message);
size_t event__pack(const Event* message, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif
