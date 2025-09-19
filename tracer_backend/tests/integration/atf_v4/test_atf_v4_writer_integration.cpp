#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

extern "C" {
#include <unistd.h>
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/atf/atf_v4_writer.h>
#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/utils/thread_registry.h>
}

namespace {

std::string random_suffix() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string result;
    for (int i = 0; i < 8; ++i) {
        result.push_back("0123456789abcdef"[dist(gen)]);
    }
    return result;
}

class ScopedTempDir {
public:
    ScopedTempDir() {
        auto base = std::filesystem::temp_directory_path();
        path_ = base / ("ada_atf_integration_" + random_suffix());
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct RegistryHarness {
    explicit RegistryHarness(size_t capacity) {
        size_t bytes = thread_registry_calculate_memory_size_with_capacity(
            static_cast<uint32_t>(capacity));
        arena = std::unique_ptr<uint8_t[]>(new uint8_t[bytes]);
        std::memset(arena.get(), 0, bytes);
        registry = thread_registry_init_with_capacity(
            arena.get(), bytes, static_cast<uint32_t>(capacity));
        EXPECT_NE(registry, nullptr);
        if (registry) {
            EXPECT_NE(thread_registry_attach(registry), nullptr);
        }
    }

    ~RegistryHarness() {
        if (registry) {
            thread_registry_deinit(registry);
        }
        ada_set_global_registry(nullptr);
    }

    RegistryHarness(const RegistryHarness&) = delete;
    RegistryHarness& operator=(const RegistryHarness&) = delete;

    std::unique_ptr<uint8_t[]> arena;
    ThreadRegistry* registry{nullptr};
};

}  // namespace

TEST(atf_v4_writer_integration,
     integration__drain_thread_stop_with_writer__then_writer_finalized) {
    ScopedTempDir temp;
    RegistryHarness harness(4);

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 2024;
    config.enable_manifest = false;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));
    EXPECT_FALSE(writer.finalized);

    DrainThread* drain = drain_thread_create(harness.registry, nullptr);
    ASSERT_NE(drain, nullptr);

    drain_thread_set_atf_writer(drain, &writer);
    ASSERT_EQ(0, drain_thread_start(drain));
    EXPECT_EQ(0, drain_thread_stop(drain));
    EXPECT_TRUE(writer.finalized);

    drain_thread_destroy(drain);
    atf_v4_writer_deinit(&writer);
}

TEST(atf_v4_writer_integration,
     integration__drain_thread_stop_with_manifest_error__then_write_errors_incremented) {
    ScopedTempDir temp;
    RegistryHarness harness(2);

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 3030;
    config.enable_manifest = true;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));
    writer.manifest_path[0] = '\0';

    DrainThread* drain = drain_thread_create(harness.registry, nullptr);
    ASSERT_NE(drain, nullptr);

    drain_thread_set_atf_writer(drain, &writer);
    ASSERT_EQ(0, drain_thread_start(drain));
    EXPECT_EQ(0, drain_thread_stop(drain));
    EXPECT_GT(atf_v4_writer_write_errors(&writer), 0u);
    EXPECT_FALSE(writer.finalized);

    drain_thread_destroy(drain);
    atf_v4_writer_deinit(&writer);
}

TEST(atf_v4_writer_integration,
     integration__flush_after_external_fd_close__then_returns_error) {
    ScopedTempDir temp;

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 44;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));

    int fd = writer.events_fd;
    ASSERT_GE(fd, 0);
    close(fd);

    EXPECT_LT(atf_v4_writer_flush(&writer), 0);

    atf_v4_writer_deinit(&writer);
}

TEST(atf_v4_writer_integration,
     integration__deinit_after_external_close__then_cleans_up_state) {
    ScopedTempDir temp;

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 45;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));
    int fd = writer.events_fd;
    ASSERT_GE(fd, 0);
    close(fd);

    atf_v4_writer_deinit(&writer);
    EXPECT_EQ(-1, writer.events_fd);
    EXPECT_FALSE(writer.initialized);
}

TEST(atf_v4_writer_integration,
     integration__finalize_after_events_fd_closed__then_error_reported) {
    ScopedTempDir temp;

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 1234;
    config.enable_manifest = false;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));

    int fd = writer.events_fd;
    ASSERT_GE(fd, 0);
    close(fd);

    EXPECT_LT(atf_v4_writer_finalize(&writer), 0);
    EXPECT_FALSE(writer.finalized);
    EXPECT_EQ(0u, atf_v4_writer_write_errors(&writer));

    atf_v4_writer_deinit(&writer);
}

TEST(atf_v4_writer_integration,
     integration__write_event_with_excessive_stack__then_error_recorded) {
    ScopedTempDir temp;

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 5678;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));

    std::array<uint8_t, ATF_V4_MAX_STACK_BYTES + 1> stack{};

    AtfV4Event event{};
    event.kind = ATF_V4_EVENT_FUNCTION_CALL;
    event.thread_id = 11;
    event.timestamp_ns = 99;
    event.payload.function_call.symbol = "overflow";
    event.payload.function_call.address = 0x1234;
    event.payload.function_call.stack_bytes = stack.data();
    event.payload.function_call.stack_size = stack.size();

    EXPECT_EQ(-EINVAL, atf_v4_writer_write_event(&writer, &event));
    EXPECT_EQ(1u, atf_v4_writer_write_errors(&writer));

    atf_v4_writer_deinit(&writer);
}

TEST(atf_v4_writer_integration,
     integration__write_signal_delivery_event__then_counters_updated) {
    ScopedTempDir temp;

    AtfV4Writer writer{};
    AtfV4WriterConfig config{};
    config.output_root = temp.path().c_str();
    config.pid = 7777;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &config));

    AtfV4Event event{};
    event.kind = ATF_V4_EVENT_SIGNAL_DELIVERY;
    event.event_id = 0;
    event.thread_id = 9;
    event.timestamp_ns = 450;
    event.payload.signal_delivery.number = 5;
    event.payload.signal_delivery.name = "SIGTRAP";
    event.payload.signal_delivery.register_count = 1;
    std::strncpy(event.payload.signal_delivery.registers[0].name, "pc",
                 sizeof(event.payload.signal_delivery.registers[0].name));
    event.payload.signal_delivery.registers[0].value = 0xFEEDu;

    ASSERT_EQ(0, atf_v4_writer_write_event(&writer, &event));
    ASSERT_EQ(0, atf_v4_writer_flush(&writer));

    EXPECT_EQ(1u, atf_v4_writer_event_count(&writer));
    EXPECT_GT(atf_v4_writer_bytes_written(&writer), 0u);

    atf_v4_writer_deinit(&writer);
}
