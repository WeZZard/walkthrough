#include "atf_v4_writer_test_helpers.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void* (*real_malloc_ptr)(size_t) = NULL;
static ssize_t (*real_write_ptr)(int, const void*, size_t) = NULL;
static int (*real_fsync_ptr)(int) = NULL;
static int (*real_rename_ptr)(const char*, const char*) = NULL;

static bool fail_next_malloc = false;
static bool fail_next_write = false;
static bool fail_next_fsync = false;
static bool fail_next_rename = false;

static void ensure_real_malloc(void) {
    if (!real_malloc_ptr) {
        real_malloc_ptr = (void* (*)(size_t))dlsym(RTLD_NEXT, "malloc");
        if (!real_malloc_ptr) {
            real_malloc_ptr = (void* (*)(size_t))dlsym(RTLD_DEFAULT, "malloc");
        }
    }
}

static void ensure_real_write(void) {
    if (!real_write_ptr) {
        real_write_ptr = (ssize_t (*)(int, const void*, size_t))dlsym(RTLD_NEXT, "write");
        if (!real_write_ptr) {
            real_write_ptr = (ssize_t (*)(int, const void*, size_t))dlsym(RTLD_DEFAULT, "write");
        }
    }
}

static void ensure_real_fsync(void) {
    if (!real_fsync_ptr) {
        real_fsync_ptr = (int (*)(int))dlsym(RTLD_NEXT, "fsync");
        if (!real_fsync_ptr) {
            real_fsync_ptr = (int (*)(int))dlsym(RTLD_DEFAULT, "fsync");
        }
    }
}

static void ensure_real_rename(void) {
    if (!real_rename_ptr) {
        real_rename_ptr = (int (*)(const char*, const char*))dlsym(RTLD_NEXT, "rename");
        if (!real_rename_ptr) {
            real_rename_ptr = (int (*)(const char*, const char*))dlsym(RTLD_DEFAULT, "rename");
        }
    }
}

static void* atf_v4_writer_test_malloc(size_t size) {
    ensure_real_malloc();
    if (fail_next_malloc) {
        fail_next_malloc = false;
        errno = ENOMEM;
        return NULL;
    }
    return real_malloc_ptr ? real_malloc_ptr(size) : NULL;
}

static ssize_t atf_v4_writer_test_write(int fd, const void* buf, size_t count) {
    ensure_real_write();
    if (fail_next_write) {
        fail_next_write = false;
        errno = EIO;
        return -1;
    }
    return real_write_ptr ? real_write_ptr(fd, buf, count) : -1;
}

static int atf_v4_writer_test_fsync(int fd) {
    ensure_real_fsync();
    if (fail_next_fsync) {
        fail_next_fsync = false;
        errno = EIO;
        return -1;
    }
    return real_fsync_ptr ? real_fsync_ptr(fd) : -1;
}

static int atf_v4_writer_test_rename(const char* oldpath, const char* newpath) {
    ensure_real_rename();
    if (fail_next_rename) {
        fail_next_rename = false;
        errno = EIO;
        return -1;
    }
    return real_rename_ptr ? real_rename_ptr(oldpath, newpath) : -1;
}

#define malloc atf_v4_writer_test_malloc
#define write atf_v4_writer_test_write
#define fsync atf_v4_writer_test_fsync
#define rename atf_v4_writer_test_rename

#define atf_v4_writer_init atf_v4_writer_init_rebound
#define atf_v4_writer_deinit atf_v4_writer_deinit_rebound
#define atf_v4_writer_write_event atf_v4_writer_write_event_rebound
#define atf_v4_writer_flush atf_v4_writer_flush_rebound
#define atf_v4_writer_finalize atf_v4_writer_finalize_rebound
#define atf_v4_writer_event_count atf_v4_writer_event_count_rebound
#define atf_v4_writer_bytes_written atf_v4_writer_bytes_written_rebound
#define atf_v4_writer_module_count atf_v4_writer_module_count_rebound
#define atf_v4_writer_session_dir atf_v4_writer_session_dir_rebound
#define atf_v4_writer_events_path atf_v4_writer_events_path_rebound
#define atf_v4_writer_manifest_path atf_v4_writer_manifest_path_rebound
#define atf_v4_writer_write_errors atf_v4_writer_write_errors_rebound
#define atf_v4_writer_register_module atf_v4_writer_register_module_rebound

#include "atf_v4_writer.c"

#undef malloc
#undef write
#undef fsync
#undef rename

#undef atf_v4_writer_init
#undef atf_v4_writer_deinit
#undef atf_v4_writer_write_event
#undef atf_v4_writer_flush
#undef atf_v4_writer_finalize
#undef atf_v4_writer_event_count
#undef atf_v4_writer_bytes_written
#undef atf_v4_writer_module_count
#undef atf_v4_writer_session_dir
#undef atf_v4_writer_events_path
#undef atf_v4_writer_manifest_path
#undef atf_v4_writer_write_errors
#undef atf_v4_writer_register_module

void atf_v4_test_reset_scratch(AtfV4ProtoScratchTest* scratch) {
    if (scratch) {
        memset(scratch, 0, sizeof(*scratch));
    }
}

int atf_v4_test_convert_function_return(const AtfV4FunctionReturn* src, AtfV4ProtoScratchTest* scratch) {
    if (!scratch) {
        return -EINVAL;
    }
    return convert_function_return(src, (ProtoScratch*)scratch);
}

int atf_v4_test_convert_signal_delivery(const AtfV4SignalDelivery* src, AtfV4ProtoScratchTest* scratch) {
    if (!scratch) {
        return -EINVAL;
    }
    return convert_signal_delivery(src, (ProtoScratch*)scratch);
}

int atf_v4_test_convert_event(const AtfV4Event* src, Event* dst, AtfV4ProtoScratchTest* scratch) {
    if (!scratch) {
        return -EINVAL;
    }
    return convert_event(src, dst, (ProtoScratch*)scratch);
}

int atf_v4_test_convert_trace_start(const AtfV4TraceStart* src, AtfV4ProtoScratchTest* scratch) {
    if (!scratch) {
        return -EINVAL;
    }
    return convert_trace_start(src, (ProtoScratch*)scratch);
}

int atf_v4_test_convert_function_call(const AtfV4FunctionCall* src, AtfV4ProtoScratchTest* scratch) {
    if (!scratch) {
        return -EINVAL;
    }
    return convert_function_call(src, (ProtoScratch*)scratch);
}

size_t atf_v4_test_encode_varint(uint64_t value, uint8_t* out) {
    return encode_varint(value, out);
}

int atf_v4_test_append_event(AtfV4Writer* writer, const Event* proto) {
    return append_event(writer, proto);
}

int atf_v4_test_write_all(int fd, const uint8_t* data, size_t len) {
    return write_all(fd, data, len);
}

int atf_v4_test_write_manifest(const AtfV4Writer* writer) {
    return write_manifest(writer);
}

void atf_v4_test_force_malloc_failure_once(void) {
    fail_next_malloc = true;
}

void atf_v4_test_force_write_failure_once(void) {
    fail_next_write = true;
}

void atf_v4_test_force_fsync_failure_once(void) {
    fail_next_fsync = true;
}

void atf_v4_test_force_rename_failure_once(void) {
    fail_next_rename = true;
}

void atf_v4_test_clear_io_faults(void) {
    fail_next_malloc = false;
    fail_next_write = false;
    fail_next_fsync = false;
    fail_next_rename = false;
}

int atf_v4_test_ensure_directory(const char* path) {
    return ensure_directory(path);
}

int atf_v4_test_flush(AtfV4Writer* writer) {
    return atf_v4_writer_flush_rebound(writer);
}
