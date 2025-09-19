#include "tracer_backend/atf/atf_v4_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "trace_schema.pb-c.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SESSION_NAME_MAX 128
#define TMP_SUFFIX ".tmp"

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
} ProtoScratch;

static uint64_t current_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static size_t encode_varint(uint64_t value, uint8_t* out) {
    size_t count = 0;
    while (value >= 0x80u) {
        out[count++] = (uint8_t)((value & 0x7Fu) | 0x80u);
        value >>= 7u;
    }
    out[count++] = (uint8_t)(value & 0x7Fu);
    return count;
}

static int ensure_directory(const char* path) {
    if (!path || path[0] == '\0') {
        return -EINVAL;
    }
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -errno;
}

static void detect_platform(AtfV4Writer* writer) {
#if defined(__APPLE__)
    strncpy(writer->manifest_os, "darwin", sizeof(writer->manifest_os));
#elif defined(__linux__)
    strncpy(writer->manifest_os, "linux", sizeof(writer->manifest_os));
#else
    strncpy(writer->manifest_os, "unknown", sizeof(writer->manifest_os));
#endif

#if defined(__aarch64__) || defined(__ARM_ARCH_8A__) || defined(__arm64__)
    strncpy(writer->manifest_arch, "aarch64", sizeof(writer->manifest_arch));
#elif defined(__x86_64__) || defined(_M_X64)
    strncpy(writer->manifest_arch, "x86_64", sizeof(writer->manifest_arch));
#else
    strncpy(writer->manifest_arch, "unknown", sizeof(writer->manifest_arch));
#endif
}

static int write_all(int fd, const uint8_t* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, data + written, len - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        written += (size_t)rc;
    }
    return 0;
}

static int convert_trace_start(const AtfV4TraceStart* src, ProtoScratch* scratch) {
    if (!src) {
        return -EINVAL;
    }
    scratch->trace_start = (TraceStart)TRACE_START__INIT;
    scratch->trace_start.executable_path = src->executable_path;
    scratch->trace_start.operating_system = src->operating_system;
    scratch->trace_start.cpu_architecture = src->cpu_architecture;

    size_t argc = src->argc;
    if (argc > ATF_V4_MAX_ARGS) {
        return -E2BIG;
    }
    for (size_t i = 0; i < argc; ++i) {
        scratch->trace_args_storage[i] = src->argv ? src->argv[i] : NULL;
    }
    scratch->trace_start.args = (char**)scratch->trace_args_storage;
    scratch->trace_start.n_args = argc;
    return 0;
}

static int convert_function_call(const AtfV4FunctionCall* src, ProtoScratch* scratch) {
    if (!src || !src->symbol) {
        return -EINVAL;
    }
    if (src->register_count > ATF_V4_MAX_REGISTERS) {
        return -E2BIG;
    }
    if (src->stack_size > ATF_V4_MAX_STACK_BYTES) {
        return -E2BIG;
    }

    scratch->function_call = (FunctionCall)FUNCTION_CALL__INIT;
    scratch->function_call.symbol = src->symbol;
    scratch->function_call.address = src->address;

    for (size_t i = 0; i < src->register_count; ++i) {
        scratch->call_entries[i].key = (char*)src->registers[i].name;
        scratch->call_entries[i].value = src->registers[i].value;
    }
    scratch->function_call.argument_registers = scratch->call_entries;
    scratch->function_call.n_argument_registers = src->register_count;

    scratch->function_call.stack_shallow_copy.len = src->stack_size;
    scratch->function_call.stack_shallow_copy.data = (uint8_t*)src->stack_bytes;
    return 0;
}

static int convert_function_return(const AtfV4FunctionReturn* src, ProtoScratch* scratch) {
    if (!src || !src->symbol) {
        return -EINVAL;
    }
    if (src->register_count > ATF_V4_MAX_REGISTERS) {
        return -E2BIG;
    }

    scratch->function_return = (FunctionReturn)FUNCTION_RETURN__INIT;
    scratch->function_return.symbol = src->symbol;
    scratch->function_return.address = src->address;

    for (size_t i = 0; i < src->register_count; ++i) {
        scratch->return_entries[i].key = (char*)src->registers[i].name;
        scratch->return_entries[i].value = src->registers[i].value;
    }
    scratch->function_return.return_registers = scratch->return_entries;
    scratch->function_return.n_return_registers = src->register_count;
    return 0;
}

static int convert_signal_delivery(const AtfV4SignalDelivery* src, ProtoScratch* scratch) {
    if (!src) {
        return -EINVAL;
    }
    if (src->register_count > ATF_V4_MAX_REGISTERS) {
        return -E2BIG;
    }

    scratch->signal_delivery = (SignalDelivery)SIGNAL_DELIVERY__INIT;
    scratch->signal_delivery.number = src->number;
    scratch->signal_delivery.name = src->name;

    for (size_t i = 0; i < src->register_count; ++i) {
        scratch->signal_entries[i].key = (char*)src->registers[i].name;
        scratch->signal_entries[i].value = src->registers[i].value;
    }
    scratch->signal_delivery.registers = scratch->signal_entries;
    scratch->signal_delivery.n_registers = src->register_count;
    return 0;
}

static int convert_event(const AtfV4Event* src, Event* dst, ProtoScratch* scratch) {
    if (!src || !dst || !scratch) {
        return -EINVAL;
    }

    *dst = (Event)EVENT__INIT;
    scratch->timestamp = (Google__Protobuf__Timestamp)GOOGLE__PROTOBUF__TIMESTAMP__INIT;
    scratch->timestamp.seconds = src->timestamp_ns / 1000000000ull;
    scratch->timestamp.nanos = (uint32_t)(src->timestamp_ns % 1000000000ull);

    dst->event_id = src->event_id;
    dst->thread_id = src->thread_id;
    dst->timestamp = &scratch->timestamp;

    switch (src->kind) {
        case ATF_V4_EVENT_TRACE_START:
            if (convert_trace_start(&src->payload.trace_start, scratch) != 0) {
                return -EINVAL;
            }
            dst->payload_case = EVENT__PAYLOAD_TRACE_START;
            dst->trace_start = &scratch->trace_start;
            break;
        case ATF_V4_EVENT_TRACE_END:
            scratch->trace_end = (TraceEnd)TRACE_END__INIT;
            scratch->trace_end.exit_code = src->payload.trace_end.exit_code;
            dst->payload_case = EVENT__PAYLOAD_TRACE_END;
            dst->trace_end = &scratch->trace_end;
            break;
        case ATF_V4_EVENT_FUNCTION_CALL:
            if (convert_function_call(&src->payload.function_call, scratch) != 0) {
                return -EINVAL;
            }
            dst->payload_case = EVENT__PAYLOAD_FUNCTION_CALL;
            dst->function_call = &scratch->function_call;
            break;
        case ATF_V4_EVENT_FUNCTION_RETURN:
            if (convert_function_return(&src->payload.function_return, scratch) != 0) {
                return -EINVAL;
            }
            dst->payload_case = EVENT__PAYLOAD_FUNCTION_RETURN;
            dst->function_return = &scratch->function_return;
            break;
        case ATF_V4_EVENT_SIGNAL_DELIVERY:
            if (convert_signal_delivery(&src->payload.signal_delivery, scratch) != 0) {
                return -EINVAL;
            }
            dst->payload_case = EVENT__PAYLOAD_SIGNAL_DELIVERY;
            dst->signal_delivery = &scratch->signal_delivery;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

static int append_event(AtfV4Writer* writer, const Event* proto) {
    uint8_t header[10];
    size_t payload_size = event__get_packed_size(proto);
    if (payload_size == 0) {
        return -EINVAL;
    }

    uint8_t* payload = (uint8_t*)malloc(payload_size);
    if (!payload) {
        return -ENOMEM;
    }

    event__pack(proto, payload);
    size_t header_size = encode_varint(payload_size, header);

    int rc = write_all(writer->events_fd, header, header_size);
    if (rc == 0) {
        rc = write_all(writer->events_fd, payload, payload_size);
    }

    free(payload);

    if (rc != 0) {
        return rc;
    }

    atomic_fetch_add_explicit(&writer->bytes_written, header_size + payload_size, memory_order_relaxed);
    atomic_fetch_add_explicit(&writer->event_count, 1, memory_order_relaxed);
    return 0;
}

static void update_trace_end(AtfV4Writer* writer, uint64_t timestamp_ns) {
    if (timestamp_ns > writer->trace_end_ns) {
        writer->trace_end_ns = timestamp_ns;
    }
}

static int write_manifest(const AtfV4Writer* writer) {
    if (!writer->manifest_enabled) {
        return 0;
    }
    if (writer->manifest_path[0] == '\0') {
        return -EINVAL;
    }

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s%sXXXXXX", writer->manifest_path, TMP_SUFFIX);
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        return -errno;
    }

    FILE* fp = fdopen(fd, "w");
    if (!fp) {
        int saved = errno;
        close(fd);
        unlink(tmp_path);
        return -saved;
    }

    uint64_t event_count = atomic_load_explicit(&writer->event_count, memory_order_relaxed);
    uint64_t bytes_written = atomic_load_explicit(&writer->bytes_written, memory_order_relaxed);
    uint32_t module_count = atomic_load_explicit(&writer->module_count, memory_order_relaxed);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"os\": \"%s\",\n", writer->manifest_os);
    fprintf(fp, "  \"arch\": \"%s\",\n", writer->manifest_arch);
    fprintf(fp, "  \"pid\": %u,\n", writer->pid);
    fprintf(fp, "  \"sessionId\": %llu,\n", (unsigned long long)writer->session_id);
    fprintf(fp, "  \"timeStartNs\": %llu,\n", (unsigned long long)writer->trace_start_ns);
    fprintf(fp, "  \"timeEndNs\": %llu,\n", (unsigned long long)writer->trace_end_ns);
    fprintf(fp, "  \"eventCount\": %llu,\n", (unsigned long long)event_count);
    fprintf(fp, "  \"bytesWritten\": %llu,\n", (unsigned long long)bytes_written);
    fprintf(fp, "  \"modules\": [");

    for (uint32_t i = 0; i < module_count; ++i) {
        fprintf(fp, "%s\"%s\"", (i == 0) ? "" : ", ", writer->modules[i]);
    }
    fprintf(fp, "]\n}");

    fflush(fp);
    fsync(fd);
    fclose(fp);

    if (rename(tmp_path, writer->manifest_path) != 0) {
        int saved = errno;
        unlink(tmp_path);
        return -saved;
    }

    return 0;
}

int atf_v4_writer_init(AtfV4Writer* writer, const AtfV4WriterConfig* config) {
    if (!writer || !config || !config->output_root) {
        return -EINVAL;
    }

    memset(writer, 0, sizeof(*writer));
    writer->events_fd = -1;
    writer->manifest_enabled = config->enable_manifest;
    writer->pid = config->pid;
    writer->session_id = config->session_id ? config->session_id : current_time_ns();

    atomic_init(&writer->event_count, 0);
    atomic_init(&writer->bytes_written, 0);
    atomic_init(&writer->write_errors, 0);
    atomic_init(&writer->module_count, 0);
    atomic_init(&writer->next_event_id, 1);

    snprintf(writer->base_path, sizeof(writer->base_path), "%s", config->output_root);

    char ada_root[PATH_MAX];
    snprintf(ada_root, sizeof(ada_root), "%s/ada_traces", writer->base_path);
    int rc = ensure_directory(writer->base_path);
    if (rc != 0) {
        return rc;
    }
    rc = ensure_directory(ada_root);
    if (rc != 0) {
        return rc;
    }

    char session_name[SESSION_NAME_MAX];
    if (config->session_label && config->session_label[0] != '\0') {
        snprintf(session_name, sizeof(session_name), "%s", config->session_label);
    } else {
        time_t now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        snprintf(session_name, sizeof(session_name),
                 "session_%04d%02d%02d_%02d%02d%02d",
                 tm_info.tm_year + 1900,
                 tm_info.tm_mon + 1,
                 tm_info.tm_mday,
                 tm_info.tm_hour,
                 tm_info.tm_min,
                 tm_info.tm_sec);
    }

    char session_path[PATH_MAX];
    snprintf(session_path, sizeof(session_path), "%s/%s", ada_root, session_name);
    rc = ensure_directory(session_path);
    if (rc != 0) {
        return rc;
    }

    snprintf(writer->session_dir, sizeof(writer->session_dir), "%s/pid_%u", session_path, writer->pid);
    rc = ensure_directory(writer->session_dir);
    if (rc != 0) {
        return rc;
    }

    snprintf(writer->events_path, sizeof(writer->events_path), "%s/events.bin", writer->session_dir);
    snprintf(writer->manifest_path, sizeof(writer->manifest_path), "%s/trace.json", writer->session_dir);

    writer->events_fd = open(writer->events_path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
    if (writer->events_fd < 0) {
        return -errno;
    }

    writer->trace_start_ns = current_time_ns();
    writer->trace_end_ns = writer->trace_start_ns;

    detect_platform(writer);
    writer->initialized = true;
    return 0;
}

void atf_v4_writer_deinit(AtfV4Writer* writer) {
    if (!writer) {
        return;
    }
    if (writer->events_fd >= 0) {
        close(writer->events_fd);
        writer->events_fd = -1;
    }
    if (writer->manifest_fp) {
        fclose(writer->manifest_fp);
        writer->manifest_fp = NULL;
    }
    writer->initialized = false;
}

int atf_v4_writer_write_event(AtfV4Writer* writer, const AtfV4Event* event) {
    if (!writer || !event || !writer->initialized) {
        return -EINVAL;
    }
    if (writer->events_fd < 0) {
        return -EBADF;
    }

    ProtoScratch scratch;
    memset(&scratch, 0, sizeof(scratch));

    Event proto;
    int rc = convert_event(event, &proto, &scratch);
    if (rc != 0) {
        atomic_fetch_add_explicit(&writer->write_errors, 1, memory_order_relaxed);
        return rc;
    }

    if (proto.event_id == 0) {
        proto.event_id = atomic_fetch_add_explicit(&writer->next_event_id, 1, memory_order_relaxed);
    }

    rc = append_event(writer, &proto);
    if (rc != 0) {
        atomic_fetch_add_explicit(&writer->write_errors, 1, memory_order_relaxed);
        return rc;
    }

    update_trace_end(writer, event->timestamp_ns);
    return 0;
}

int atf_v4_writer_flush(AtfV4Writer* writer) {
    if (!writer) {
        return -EINVAL;
    }
    if (writer->events_fd >= 0) {
        if (fsync(writer->events_fd) != 0) {
            return -errno;
        }
    }
    return 0;
}

int atf_v4_writer_finalize(AtfV4Writer* writer) {
    if (!writer || !writer->initialized) {
        return -EINVAL;
    }
    if (writer->finalized) {
        return 0;
    }

    int rc = atf_v4_writer_flush(writer);
    if (rc != 0) {
        return rc;
    }

    uint64_t now = current_time_ns();
    if (now > writer->trace_end_ns) {
        writer->trace_end_ns = now;
    }

    rc = write_manifest(writer);
    if (rc != 0) {
        atomic_fetch_add_explicit(&writer->write_errors, 1, memory_order_relaxed);
        return rc;
    }

    writer->finalized = true;
    return 0;
}

uint64_t atf_v4_writer_event_count(const AtfV4Writer* writer) {
    if (!writer) {
        return 0;
    }
    return atomic_load_explicit(&writer->event_count, memory_order_relaxed);
}

uint64_t atf_v4_writer_bytes_written(const AtfV4Writer* writer) {
    if (!writer) {
        return 0;
    }
    return atomic_load_explicit(&writer->bytes_written, memory_order_relaxed);
}

uint32_t atf_v4_writer_module_count(const AtfV4Writer* writer) {
    if (!writer) {
        return 0;
    }
    return (uint32_t)atomic_load_explicit(&writer->module_count, memory_order_relaxed);
}

const char* atf_v4_writer_session_dir(const AtfV4Writer* writer) {
    if (!writer) {
        return NULL;
    }
    return writer->session_dir;
}

const char* atf_v4_writer_events_path(const AtfV4Writer* writer) {
    if (!writer) {
        return NULL;
    }
    return writer->events_path;
}

const char* atf_v4_writer_manifest_path(const AtfV4Writer* writer) {
    if (!writer) {
        return NULL;
    }
    return writer->manifest_path;
}

uint32_t atf_v4_writer_write_errors(const AtfV4Writer* writer) {
    if (!writer) {
        return 0;
    }
    return (uint32_t)atomic_load_explicit(&writer->write_errors, memory_order_relaxed);
}

int atf_v4_writer_register_module(AtfV4Writer* writer, const char* module_uuid) {
    if (!writer || !module_uuid || module_uuid[0] == '\0') {
        return -EINVAL;
    }

    uint32_t count = atomic_load_explicit(&writer->module_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; ++i) {
        if (strncmp(writer->modules[i], module_uuid, ATF_V4_UUID_STRING_SIZE) == 0) {
            return 0;
        }
    }

    if (count >= ATF_V4_MAX_MODULES) {
        return -ENOSPC;
    }

    snprintf(writer->modules[count], ATF_V4_UUID_STRING_SIZE, "%s", module_uuid);
    atomic_store_explicit(&writer->module_count, count + 1, memory_order_release);
    return 0;
}
