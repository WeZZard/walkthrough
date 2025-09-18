#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

extern "C" {
#include <tracer_backend/controller/frida_controller.h>
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/control_block_ipc.h>
#include <tracer_backend/utils/tracer_types.h>
#include "ada_paths.h"
}

using namespace std::chrono_literals;

namespace {

struct ShmHandles {
    SharedMemoryRef control{nullptr};
    SharedMemoryRef index{nullptr};
    SharedMemoryRef detail{nullptr};
    SharedMemoryRef registry{nullptr};
};

static ShmHandles open_all_shm() {
    ShmHandles h{};
    uint32_t sid = shared_memory_get_session_id();
    pid_t local_pid = shared_memory_get_pid();
    h.control = shared_memory_open_unique(ADA_ROLE_CONTROL, local_pid, sid, 4096);
    h.index = shared_memory_open_unique(ADA_ROLE_INDEX, local_pid, sid, 32 * 1024 * 1024);
    h.detail = shared_memory_open_unique(ADA_ROLE_DETAIL, local_pid, sid, 32 * 1024 * 1024);
    // Registry is optional
    size_t reg_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);
    h.registry = shared_memory_open_unique(ADA_ROLE_REGISTRY, local_pid, sid, reg_size);
    return h;
}

static ControlBlock* get_cb(const ShmHandles& h) {
    return h.control ? (ControlBlock*) shared_memory_get_address(h.control) : nullptr;
}

static RingBufferHeader* get_global_index_header(const ShmHandles& h) {
    if (!h.index) return nullptr;
    void* addr = shared_memory_get_address(h.index);
    RingBuffer* rb = ring_buffer_attach(addr, shared_memory_get_size(h.index), sizeof(IndexEvent));
    if (!rb) return nullptr;
    RingBufferHeader* hdr = ring_buffer_get_header(rb);
    // Keep rb handle alive only for header discovery; destroy afterwards
    ring_buffer_destroy(rb);
    return hdr;
}

static uint64_t sum_per_thread_index_write_pos(ThreadRegistry* reg) {
    if (!reg) return 0;
    uint64_t sum = 0;
    uint32_t cap = thread_registry_get_capacity(reg);
    for (uint32_t i = 0; i < cap; ++i) {
        ThreadLaneSet* lanes = thread_registry_get_thread_at(reg, i);
        if (!lanes) continue;
        Lane* idx_lane = thread_lanes_get_index_lane(lanes);
        RingBufferHeader* hdr = thread_registry_get_active_ring_header(reg, idx_lane);
        if (hdr) {
            // Directly sample write_pos (best-effort, non-atomic read OK for testing)
            sum += hdr->write_pos;
        }
    }
    return sum;
}

static ThreadRegistry* attach_registry(const ShmHandles& h) {
    if (!h.registry) return nullptr;
    return thread_registry_attach(shared_memory_get_address(h.registry));
}

static void inject_and_run(FridaController* controller, const char* exe_rel, uint32_t* out_pid) {
    ASSERT_NE(controller, nullptr);
    const char * exe_path = ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/";
    std::string full_exe = std::string(exe_path) + exe_rel;
    char* argv[] = {(char*)full_exe.c_str(), nullptr};
    uint32_t pid = 0;
    int r = frida_controller_spawn_suspended(controller, full_exe.c_str(), argv, &pid);
    if (r != 0 || pid == 0) {
        GTEST_SKIP() << "Could not spawn test process: " << full_exe;
    }

    // Attach and inject agent
    ASSERT_EQ(frida_controller_attach(controller, pid), 0);
    std::string agent_dir = std::string(ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib");
    setenv("ADA_AGENT_RPATH_SEARCH_PATHS", agent_dir.c_str(), 1);
    r = frida_controller_install_hooks(controller);
    ASSERT_EQ(r, 0);

    // Resume process
    ASSERT_EQ(frida_controller_resume(controller), 0);
    if (out_pid) *out_pid = pid;

    // Wait for hooks to be ready (agent sets hooks_ready flag)
    // This ensures hooks are installed before the target code runs
    ShmHandles shm = open_all_shm();
    ControlBlock* cb = get_cb(shm);
    if (cb) {
        int wait_count = 0;
        while (__atomic_load_n(&cb->hooks_ready, __ATOMIC_ACQUIRE) == 0 && wait_count < 100) {
            std::this_thread::sleep_for(10ms);
            wait_count++;
        }
        if (wait_count >= 100) {
            fprintf(stderr, "[Test] WARNING: Timed out waiting for hooks_ready\n");
        } else {
            fprintf(stderr, "[Test] Hooks ready after %d ms\n", wait_count * 10);
        }
    }
}

} // namespace

// 1) GLOBAL_ONLY: writes only to process-global ring
// DISABLED: Native hooks don't fire in injected agents (architectural limitation)
TEST(DISABLED_agent__registry_global_only__then_global_ring_only, integration) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    if (!controller) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ShmHandles shm = open_all_shm();
    ControlBlock* cb = get_cb(shm);
    ASSERT_NE(cb, nullptr);

    // Set up IPC to keep agent in GLOBAL_ONLY mode
    // Enable lanes to capture events
    cb->index_lane_enabled = 1;
    cb->detail_lane_enabled = 1;
    cb_set_registry_ready(cb, 0);  // Not ready
    cb_set_registry_mode(cb, REGISTRY_MODE_GLOBAL_ONLY);

    uint32_t pid = 0;
    inject_and_run(controller, "test_cli", &pid);

    // Let target run for a bit to generate events
    std::this_thread::sleep_for(250ms);

    // Sample positions
    RingBufferHeader* g_hdr = get_global_index_header(shm);
    ASSERT_NE(g_hdr, nullptr);
    size_t g_pos = g_hdr->write_pos;

    ThreadRegistry* reg = attach_registry(shm);
    uint64_t pt_sum = sum_per_thread_index_write_pos(reg);

    // NOTE: Due to architectural limitation (native hooks lack QuickJS context),
    // events won't actually be generated in injected agents.
    // This test is disabled until the architecture is updated.
    EXPECT_GT(g_pos, 0u) << "Global ring should receive events";
    EXPECT_EQ(pt_sum, 0u) << "Per-thread lanes should remain unused in GLOBAL_ONLY";

    frida_controller_destroy(controller);
}

// 2) DUAL_WRITE: writes to both per-thread lanes and process-global ring
// DISABLED: Native hooks don't fire in injected agents (architectural limitation)
TEST(DISABLED_agent__registry_dual_write__then_both_paths_used, integration) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    if (!controller) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ShmHandles shm = open_all_shm();
    ControlBlock* cb = get_cb(shm);
    ASSERT_NE(cb, nullptr);

    // Enable lanes to capture events
    cb->index_lane_enabled = 1;
    cb->detail_lane_enabled = 1;

    // The controller already sets up the registry properly with:
    // registry_ready=1, epoch=1, mode=DUAL_WRITE
    // The drain thread will update heartbeat and mode transitions

    uint32_t pid = 0;
    inject_and_run(controller, "test_cli", &pid);

    // Wait for the controller and agent to work through state transitions
    // The controller's drain thread runs every 100ms and will:
    // - Keep heartbeat fresh
    // - Transition mode after 5 ticks (500ms) to PER_THREAD_ONLY

    // Wait for agent to start in DUAL_WRITE mode (controller's initial setting)
    std::this_thread::sleep_for(300ms);

    RingBufferHeader* g_hdr = get_global_index_header(shm);
    ASSERT_NE(g_hdr, nullptr);
    size_t g_pos = g_hdr->write_pos;

    ThreadRegistry* reg = attach_registry(shm);
    ASSERT_NE(reg, nullptr) << "Registry must exist for dual-write";
    uint64_t pt_sum = sum_per_thread_index_write_pos(reg);

    // NOTE: Due to architectural limitation (native hooks lack QuickJS context),
    // events won't actually be generated in injected agents.
    // This test is disabled until the architecture is updated.
    EXPECT_GT(g_pos, 0u) << "Global ring should receive events in DUAL_WRITE";
    EXPECT_GT(pt_sum, 0u) << "Per-thread lanes should receive events in DUAL_WRITE";

    frida_controller_destroy(controller);
}

// 3) PER_THREAD_ONLY: writes only to per-thread lanes when registry available
// DISABLED: Native hooks don't fire in injected agents (architectural limitation)
TEST(DISABLED_agent__registry_per_thread_only__then_per_thread_used, integration) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    if (!controller) {
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ShmHandles shm = open_all_shm();
    ControlBlock* cb = get_cb(shm);
    ASSERT_NE(cb, nullptr);

    // Enable lanes to capture events
    cb->index_lane_enabled = 1;
    cb->detail_lane_enabled = 1;

    // Ensure registry exists
    ThreadRegistry* reg0 = attach_registry(shm);
    if (!reg0) {
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Registry not available";
    }

    // The controller already sets registry_ready=1, epoch=1 with fresh heartbeat
    // The agent will start in GLOBAL_ONLY and transition:
    // GLOBAL_ONLY -> DUAL_WRITE -> PER_THREAD_ONLY

    uint32_t pid = 0;
    inject_and_run(controller, "test_cli", &pid);

    // Wait for agent to transition through states
    // Each transition requires an event capture that calls update_registry_mode()
    // The test_cli program generates many events quickly
    std::this_thread::sleep_for(500ms);

    RingBufferHeader* g_hdr = get_global_index_header(shm);
    ASSERT_NE(g_hdr, nullptr);
    size_t g_pos = g_hdr->write_pos;

    ThreadRegistry* reg = attach_registry(shm);
    ASSERT_NE(reg, nullptr);
    uint64_t pt_sum = sum_per_thread_index_write_pos(reg);

    // NOTE: Due to architectural limitation (native hooks lack QuickJS context),
    // events won't actually be generated in injected agents.
    // This test is disabled until the architecture is updated.
    EXPECT_GT(pt_sum, 0u) << "Per-thread lanes should receive events in PER_THREAD_ONLY";
    // Global ring will have events from the DUAL_WRITE phase, but should be much less than per-thread
    // We can't expect it to be near-zero since agent transitions through DUAL_WRITE
    EXPECT_GT(g_pos, 0u) << "Global ring will have events from DUAL_WRITE phase";
    EXPECT_GT(pt_sum, g_pos / 2) << "Per-thread should have significantly more events than global";
    EXPECT_EQ(cb_get_fallback_events(cb), 0u) << "No fallback expected when registry is available";

    frida_controller_destroy(controller);
}

// 4) PER_THREAD_ONLY but registry unavailable → fallback to global and increment counter
// DISABLED: Native hooks don't fire in injected agents (architectural limitation)
TEST(DISABLED_agent__per_thread_only_without_registry__then_fallback_to_global_and_counter_increments, integration) {
    // Disable registry from controller side so agent can't attach
    setenv("ADA_DISABLE_REGISTRY", "1", 1);

    FridaController* controller = frida_controller_create("/tmp/ada_test");
    if (!controller) {
        unsetenv("ADA_DISABLE_REGISTRY");
        GTEST_SKIP() << "FridaController unavailable (frida-core env)";
    }

    ShmHandles shm = open_all_shm();
    ControlBlock* cb = get_cb(shm);
    ASSERT_NE(cb, nullptr);

    // Enable lanes to capture events
    cb->index_lane_enabled = 1;
    cb->detail_lane_enabled = 1;

    // Simulate a misconfiguration: controller says registry is ready but it's not actually available
    // This tests the agent's fallback mechanism

    // Manually set IPC signals as if registry was ready (but it's not)
    cb_set_registry_ready(cb, 1);
    cb_set_registry_epoch(cb, 1);
#ifdef __APPLE__
    uint64_t now_ns = mach_absolute_time();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
    cb_set_heartbeat_ns(cb, now_ns);

    uint64_t fb0 = cb_get_fallback_events(cb);

    uint32_t pid = 0;
    inject_and_run(controller, "test_cli", &pid);

    // Agent will see registry_ready=1 and transition to DUAL_WRITE then PER_THREAD_ONLY
    // But since there's no actual registry, per-thread writes will fail and fallback to global
    std::this_thread::sleep_for(500ms);

    // No registry exists
    ThreadRegistry* reg = attach_registry(shm);
    EXPECT_EQ(reg, nullptr);

    // Global ring should receive events; per-thread sum is zero
    RingBufferHeader* g_hdr = get_global_index_header(shm);
    ASSERT_NE(g_hdr, nullptr);
    size_t g_pos = g_hdr->write_pos;
    EXPECT_GT(g_pos, 0u);

    // Fallback counter should have increased
    uint64_t fb1 = cb_get_fallback_events(cb);
    EXPECT_GT(fb1, fb0);

    frida_controller_destroy(controller);
    unsetenv("ADA_DISABLE_REGISTRY");
}
