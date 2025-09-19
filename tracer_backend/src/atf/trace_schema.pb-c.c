#include "trace_schema.pb-c.h"

#include <stdbool.h>
#include <string.h>

#define WIRE_TYPE_VARINT           0u
#define WIRE_TYPE_LENGTH_DELIMITED 2u

static size_t pack_varint(uint64_t value, uint8_t* out) {
    size_t count = 0;
    while (value >= 0x80u) {
        out[count++] = (uint8_t)((value & 0x7Fu) | 0x80u);
        value >>= 7u;
    }
    out[count++] = (uint8_t)(value & 0x7Fu);
    return count;
}

static size_t sizeof_varint(uint64_t value) {
    size_t size = 1;
    while (value >= 0x80u) {
        value >>= 7u;
        size++;
    }
    return size;
}

static size_t pack_field_key(uint32_t field_number, uint32_t wire_type, uint8_t* out) {
    uint32_t key = (field_number << 3u) | (wire_type & 0x7u);
    return pack_varint(key, out);
}

static size_t sizeof_field_key(uint32_t field_number, uint32_t wire_type) {
    uint32_t key = (field_number << 3u) | (wire_type & 0x7u);
    return sizeof_varint(key);
}

static size_t sizeof_string(const char* value) {
    if (!value) {
        return sizeof_varint(0);
    }
    size_t len = strlen(value);
    return sizeof_varint(len) + len;
}

static size_t pack_string(const char* value, uint8_t* out) {
    if (!value) {
        out[0] = 0;
        return 1;
    }
    size_t len = strlen(value);
    size_t used = pack_varint(len, out);
    memcpy(out + used, value, len);
    return used + len;
}

static size_t sizeof_bytes(size_t len) {
    return sizeof_varint(len) + len;
}

static size_t pack_bytes(const uint8_t* data, size_t len, uint8_t* out) {
    size_t used = pack_varint(len, out);
    if (len > 0 && data) {
        memcpy(out + used, data, len);
    }
    return used + len;
}

static size_t timestamp_get_packed_size(const Google__Protobuf__Timestamp* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    size += sizeof_field_key(1, WIRE_TYPE_VARINT) + sizeof_varint(message->seconds);
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(message->nanos);
    return size;
}

static size_t timestamp_pack(const Google__Protobuf__Timestamp* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(message->seconds, out + used);
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(message->nanos, out + used);
    return used;
}

static size_t argument_entry_get_packed_size(const FunctionCall__ArgumentRegistersEntry* entry) {
    if (!entry) {
        return 0;
    }
    size_t size = 0;
    size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(entry->key);
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(entry->value);
    return size;
}

static size_t argument_entry_pack(const FunctionCall__ArgumentRegistersEntry* entry, uint8_t* out) {
    if (!entry) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
    used += pack_string(entry->key, out + used);
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(entry->value, out + used);
    return used;
}

static size_t return_entry_get_packed_size(const FunctionReturn__ReturnRegistersEntry* entry) {
    if (!entry) {
        return 0;
    }
    size_t size = 0;
    size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(entry->key);
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(entry->value);
    return size;
}

static size_t return_entry_pack(const FunctionReturn__ReturnRegistersEntry* entry, uint8_t* out) {
    if (!entry) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
    used += pack_string(entry->key, out + used);
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(entry->value, out + used);
    return used;
}

static size_t signal_entry_get_packed_size(const SignalDelivery__RegistersEntry* entry) {
    if (!entry) {
        return 0;
    }
    size_t size = 0;
    size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(entry->key);
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(entry->value);
    return size;
}

static size_t signal_entry_pack(const SignalDelivery__RegistersEntry* entry, uint8_t* out) {
    if (!entry) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
    used += pack_string(entry->key, out + used);
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(entry->value, out + used);
    return used;
}

size_t trace_start__get_packed_size(const TraceStart* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    if (message->executable_path) {
        size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->executable_path);
    }
    for (size_t i = 0; i < message->n_args; ++i) {
        const char* arg = message->args ? message->args[i] : NULL;
        size += sizeof_field_key(2, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(arg);
    }
    if (message->operating_system) {
        size += sizeof_field_key(3, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->operating_system);
    }
    if (message->cpu_architecture) {
        size += sizeof_field_key(4, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->cpu_architecture);
    }
    return size;
}

size_t trace_start__pack(const TraceStart* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    if (message->executable_path) {
        used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->executable_path, out + used);
    }
    for (size_t i = 0; i < message->n_args; ++i) {
        const char* arg = message->args ? message->args[i] : NULL;
        used += pack_field_key(2, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(arg, out + used);
    }
    if (message->operating_system) {
        used += pack_field_key(3, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->operating_system, out + used);
    }
    if (message->cpu_architecture) {
        used += pack_field_key(4, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->cpu_architecture, out + used);
    }
    return used;
}

size_t trace_end__get_packed_size(const TraceEnd* message) {
    if (!message) {
        return 0;
    }
    if (message->exit_code == 0) {
        return 0;
    }
    return sizeof_field_key(1, WIRE_TYPE_VARINT) + sizeof_varint((uint64_t)message->exit_code);
}

size_t trace_end__pack(const TraceEnd* message, uint8_t* out) {
    if (!message || message->exit_code == 0) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_VARINT, out + used);
    used += pack_varint((uint64_t)message->exit_code, out + used);
    return used;
}

size_t function_call__get_packed_size(const FunctionCall* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    if (message->symbol) {
        size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->symbol);
    }
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(message->address);
    for (size_t i = 0; i < message->n_argument_registers; ++i) {
        size_t entry_size = argument_entry_get_packed_size(&message->argument_registers[i]);
        size += sizeof_field_key(3, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(entry_size) + entry_size;
    }
    if (message->stack_shallow_copy.len > 0) {
        size += sizeof_field_key(4, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_bytes(message->stack_shallow_copy.len);
    }
    return size;
}

size_t function_call__pack(const FunctionCall* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    if (message->symbol) {
        used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->symbol, out + used);
    }
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(message->address, out + used);
    for (size_t i = 0; i < message->n_argument_registers; ++i) {
        const FunctionCall__ArgumentRegistersEntry* entry = &message->argument_registers[i];
        size_t entry_size = argument_entry_get_packed_size(entry);
        used += pack_field_key(3, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_varint(entry_size, out + used);
        used += argument_entry_pack(entry, out + used);
    }
    if (message->stack_shallow_copy.len > 0) {
        used += pack_field_key(4, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_bytes(message->stack_shallow_copy.data, message->stack_shallow_copy.len, out + used);
    }
    return used;
}

size_t function_return__get_packed_size(const FunctionReturn* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    if (message->symbol) {
        size += sizeof_field_key(1, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->symbol);
    }
    size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint(message->address);
    for (size_t i = 0; i < message->n_return_registers; ++i) {
        size_t entry_size = return_entry_get_packed_size(&message->return_registers[i]);
        size += sizeof_field_key(3, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(entry_size) + entry_size;
    }
    return size;
}

size_t function_return__pack(const FunctionReturn* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    if (message->symbol) {
        used += pack_field_key(1, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->symbol, out + used);
    }
    used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
    used += pack_varint(message->address, out + used);
    for (size_t i = 0; i < message->n_return_registers; ++i) {
        const FunctionReturn__ReturnRegistersEntry* entry = &message->return_registers[i];
        size_t entry_size = return_entry_get_packed_size(entry);
        used += pack_field_key(3, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_varint(entry_size, out + used);
        used += return_entry_pack(entry, out + used);
    }
    return used;
}

size_t signal_delivery__get_packed_size(const SignalDelivery* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    size += sizeof_field_key(1, WIRE_TYPE_VARINT) + sizeof_varint((uint64_t)message->number);
    if (message->name) {
        size += sizeof_field_key(2, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_string(message->name);
    }
    for (size_t i = 0; i < message->n_registers; ++i) {
        size_t entry_size = signal_entry_get_packed_size(&message->registers[i]);
        size += sizeof_field_key(3, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(entry_size) + entry_size;
    }
    return size;
}

size_t signal_delivery__pack(const SignalDelivery* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    used += pack_field_key(1, WIRE_TYPE_VARINT, out + used);
    used += pack_varint((uint64_t)message->number, out + used);
    if (message->name) {
        used += pack_field_key(2, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_string(message->name, out + used);
    }
    for (size_t i = 0; i < message->n_registers; ++i) {
        const SignalDelivery__RegistersEntry* entry = &message->registers[i];
        size_t entry_size = signal_entry_get_packed_size(entry);
        used += pack_field_key(3, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_varint(entry_size, out + used);
        used += signal_entry_pack(entry, out + used);
    }
    return used;
}

size_t event__get_packed_size(const Event* message) {
    if (!message) {
        return 0;
    }
    size_t size = 0;
    if (message->event_id != 0) {
        size += sizeof_field_key(1, WIRE_TYPE_VARINT) + sizeof_varint(message->event_id);
    }
    if (message->thread_id != 0) {
        size += sizeof_field_key(2, WIRE_TYPE_VARINT) + sizeof_varint((uint64_t)message->thread_id);
    }
    if (message->timestamp) {
        size_t ts_size = timestamp_get_packed_size(message->timestamp);
        size += sizeof_field_key(3, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(ts_size) + ts_size;
    }

    switch (message->payload_case) {
        case EVENT__PAYLOAD_TRACE_START: {
            size_t payload = trace_start__get_packed_size(message->trace_start);
            size += sizeof_field_key(10, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(payload) + payload;
            break;
        }
        case EVENT__PAYLOAD_TRACE_END: {
            size_t payload = trace_end__get_packed_size(message->trace_end);
            size += sizeof_field_key(11, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(payload) + payload;
            break;
        }
        case EVENT__PAYLOAD_FUNCTION_CALL: {
            size_t payload = function_call__get_packed_size(message->function_call);
            size += sizeof_field_key(12, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(payload) + payload;
            break;
        }
        case EVENT__PAYLOAD_FUNCTION_RETURN: {
            size_t payload = function_return__get_packed_size(message->function_return);
            size += sizeof_field_key(13, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(payload) + payload;
            break;
        }
        case EVENT__PAYLOAD_SIGNAL_DELIVERY: {
            size_t payload = signal_delivery__get_packed_size(message->signal_delivery);
            size += sizeof_field_key(14, WIRE_TYPE_LENGTH_DELIMITED) + sizeof_varint(payload) + payload;
            break;
        }
        case EVENT__PAYLOAD__NOT_SET:
        default:
            break;
    }

    return size;
}

size_t event__pack(const Event* message, uint8_t* out) {
    if (!message) {
        return 0;
    }
    size_t used = 0;
    if (message->event_id != 0) {
        used += pack_field_key(1, WIRE_TYPE_VARINT, out + used);
        used += pack_varint(message->event_id, out + used);
    }
    if (message->thread_id != 0) {
        used += pack_field_key(2, WIRE_TYPE_VARINT, out + used);
        used += pack_varint((uint64_t)message->thread_id, out + used);
    }
    if (message->timestamp) {
        size_t ts_size = timestamp_get_packed_size(message->timestamp);
        used += pack_field_key(3, WIRE_TYPE_LENGTH_DELIMITED, out + used);
        used += pack_varint(ts_size, out + used);
        used += timestamp_pack(message->timestamp, out + used);
    }

    switch (message->payload_case) {
        case EVENT__PAYLOAD_TRACE_START: {
            size_t payload_size = trace_start__get_packed_size(message->trace_start);
            used += pack_field_key(10, WIRE_TYPE_LENGTH_DELIMITED, out + used);
            used += pack_varint(payload_size, out + used);
            used += trace_start__pack(message->trace_start, out + used);
            break;
        }
        case EVENT__PAYLOAD_TRACE_END: {
            size_t payload_size = trace_end__get_packed_size(message->trace_end);
            used += pack_field_key(11, WIRE_TYPE_LENGTH_DELIMITED, out + used);
            used += pack_varint(payload_size, out + used);
            used += trace_end__pack(message->trace_end, out + used);
            break;
        }
        case EVENT__PAYLOAD_FUNCTION_CALL: {
            size_t payload_size = function_call__get_packed_size(message->function_call);
            used += pack_field_key(12, WIRE_TYPE_LENGTH_DELIMITED, out + used);
            used += pack_varint(payload_size, out + used);
            used += function_call__pack(message->function_call, out + used);
            break;
        }
        case EVENT__PAYLOAD_FUNCTION_RETURN: {
            size_t payload_size = function_return__get_packed_size(message->function_return);
            used += pack_field_key(13, WIRE_TYPE_LENGTH_DELIMITED, out + used);
            used += pack_varint(payload_size, out + used);
            used += function_return__pack(message->function_return, out + used);
            break;
        }
        case EVENT__PAYLOAD_SIGNAL_DELIVERY: {
            size_t payload_size = signal_delivery__get_packed_size(message->signal_delivery);
            used += pack_field_key(14, WIRE_TYPE_LENGTH_DELIMITED, out + used);
            used += pack_varint(payload_size, out + used);
            used += signal_delivery__pack(message->signal_delivery, out + used);
            break;
        }
        case EVENT__PAYLOAD__NOT_SET:
        default:
            break;
    }

    return used;
}
