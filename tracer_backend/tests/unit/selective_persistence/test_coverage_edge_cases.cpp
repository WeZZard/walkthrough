#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <atomic>
#include <filesystem>
#include <regex>

// Forward declarations for functions we'll intercept
extern "C" {
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
}

#include <tracer_backend/selective_persistence/detail_lane_control.h>
#include <tracer_backend/selective_persistence/marking_policy.h>

// Thread-local flag to control function behavior
static thread_local bool g_force_get_detail_lane_null = false;
static thread_local bool g_force_swap_active_fail = false;
static thread_local bool g_force_fopen_fail = false;
static thread_local bool g_force_fprintf_fail = false;
static thread_local bool g_force_fflush_fail = false;
static thread_local bool g_force_fclose_fail = false;

// Hook for thread_lanes_get_detail_lane
extern "C" Lane* thread_lanes_get_detail_lane_original(ThreadLaneSet* lanes);
extern "C" Lane* thread_lanes_get_detail_lane(ThreadLaneSet* lanes) {
    if (g_force_get_detail_lane_null) {
        return nullptr;
    }
    // Call through to the original implementation
    return thread_lanes_get_detail_lane_original(lanes);
}

// Hook for ring_pool_swap_active
extern "C" bool ring_pool_swap_active_original(RingPool* pool, size_t* out_submitted_ring_idx);
extern "C" bool ring_pool_swap_active(RingPool* pool, size_t* out_submitted_ring_idx) {
    if (g_force_swap_active_fail) {
        return false;
    }
    return ring_pool_swap_active_original(pool, out_submitted_ring_idx);
}

// Hook for stdio functions
#ifdef __cplusplus
extern "C" {
#endif

FILE* fopen(const char* path, const char* mode) {
    if (g_force_fopen_fail) {
        errno = EIO;
        return nullptr;
    }
    // Call real fopen
    typedef FILE* (*fopen_t)(const char*, const char*);
    static fopen_t real_fopen = (fopen_t)dlsym(RTLD_NEXT, "fopen");
    return real_fopen(path, mode);
}

int fprintf(FILE* stream, const char* format, ...) {
    if (g_force_fprintf_fail) {
        return -1;
    }
    // Call real fprintf
    va_list args;
    va_start(args, format);
    typedef int (*vfprintf_t)(FILE*, const char*, va_list);
    static vfprintf_t real_vfprintf = (vfprintf_t)dlsym(RTLD_NEXT, "vfprintf");
    int ret = real_vfprintf(stream, format, args);
    va_end(args);
    return ret;
}

int fflush(FILE* stream) {
    if (g_force_fflush_fail) {
        errno = EIO;
        return EOF;
    }
    typedef int (*fflush_t)(FILE*);
    static fflush_t real_fflush = (fflush_t)dlsym(RTLD_NEXT, "fflush");
    return real_fflush(stream);
}

int fclose(FILE* stream) {
    if (g_force_fclose_fail) {
        errno = EIO;
        return EOF;
    }
    typedef int (*fclose_t)(FILE*);
    static fclose_t real_fclose = (fclose_t)dlsym(RTLD_NEXT, "fclose");
    return real_fclose(stream);
}

#ifdef __cplusplus
}
#endif

#include <dlfcn.h>

namespace {

// Test fixture for edge case testing
class CoverageEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset all flags
        g_force_get_detail_lane_null = false;
        g_force_swap_active_fail = false;
        g_force_fopen_fail = false;
        g_force_fprintf_fail = false;
        g_force_fflush_fail = false;
        g_force_fclose_fail = false;
    }

    void TearDown() override {
        // Reset all flags
        g_force_get_detail_lane_null = false;
        g_force_swap_active_fail = false;
        g_force_fopen_fail = false;
        g_force_fprintf_fail = false;
        g_force_fflush_fail = false;
        g_force_fclose_fail = false;
    }
};

// Test for detail_lane_control.cpp lines 110-111
TEST_F(CoverageEdgeCaseTest, detail_lane_control_create__null_lane__then_returns_null) {
    // Create minimal dependencies
    size_t registry_size = thread_registry_calculate_memory_size_with_capacity(1);
    auto arena = std::make_unique<uint8_t[]>(registry_size);
    ThreadRegistry* registry = thread_registry_init_with_capacity(arena.get(), registry_size, 1);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_get_or_create_lane_set(registry, 1);
    ASSERT_NE(lanes, nullptr);

    RingPool* pool = ring_pool_create(4, 1024);
    ASSERT_NE(pool, nullptr);

    MarkingPolicy* policy = marking_policy_create();
    ASSERT_NE(policy, nullptr);

    // Force thread_lanes_get_detail_lane to return nullptr
    g_force_get_detail_lane_null = true;

    // This should hit lines 110-111
    DetailLaneControl* control = detail_lane_control_create(registry, lanes, pool, policy);
    EXPECT_EQ(control, nullptr);

    // Cleanup
    marking_policy_destroy(policy);
    ring_pool_destroy(pool);
    thread_registry_destroy(registry);
}

// Test for detail_lane_control.cpp lines 286-288 - null pool in selective_swap
TEST_F(CoverageEdgeCaseTest, selective_swap__pool_destroyed__then_state_error) {
    // Create control with valid dependencies
    size_t registry_size = thread_registry_calculate_memory_size_with_capacity(1);
    auto arena = std::make_unique<uint8_t[]>(registry_size);
    ThreadRegistry* registry = thread_registry_init_with_capacity(arena.get(), registry_size, 1);
    ThreadLaneSet* lanes = thread_registry_get_or_create_lane_set(registry, 1);
    RingPool* pool = ring_pool_create(4, 1024);
    MarkingPolicy* policy = marking_policy_create();

    DetailLaneControl* control = detail_lane_control_create(registry, lanes, pool, policy);
    ASSERT_NE(control, nullptr);

    // Simulate pool being null by corrupting the internal state
    // This requires accessing internals - we'll use a different approach
    // Instead, we'll test the ring_pool_swap_active failure path

    size_t idx = 0;
    g_force_swap_active_fail = true;

    // This should hit lines 294-296
    bool result = detail_lane_control_selective_swap(control, &idx);
    EXPECT_FALSE(result);
    EXPECT_EQ(detail_lane_control_get_last_error(control), DETAIL_LANE_CONTROL_ERROR_STATE);

    // Cleanup
    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
    ring_pool_destroy(pool);
    thread_registry_destroy(registry);
}

// Test for detail_lane_control.cpp lines 353, 355-356, 358-359, 362-363 - I/O failures
TEST_F(CoverageEdgeCaseTest, write_metadata__io_failures__then_error_recorded) {
    // Create control
    size_t registry_size = thread_registry_calculate_memory_size_with_capacity(1);
    auto arena = std::make_unique<uint8_t[]>(registry_size);
    ThreadRegistry* registry = thread_registry_init_with_capacity(arena.get(), registry_size, 1);
    ThreadLaneSet* lanes = thread_registry_get_or_create_lane_set(registry, 1);
    RingPool* pool = ring_pool_create(4, 1024);
    MarkingPolicy* policy = marking_policy_create();
    DetailLaneControl* control = detail_lane_control_create(registry, lanes, pool, policy);

    // Create temp directory
    auto temp_dir = std::filesystem::temp_directory_path() / "test_metadata";
    std::filesystem::create_directories(temp_dir);

    // Test fprintf failure (lines 353)
    g_force_fprintf_fail = true;
    bool result = detail_lane_control_write_metadata(control, temp_dir.string().c_str());
    EXPECT_FALSE(result);
    EXPECT_EQ(detail_lane_control_get_last_error(control), DETAIL_LANE_CONTROL_ERROR_IO);
    g_force_fprintf_fail = false;

    // Test fflush failure (lines 355-356)
    g_force_fflush_fail = true;
    result = detail_lane_control_write_metadata(control, temp_dir.string().c_str());
    EXPECT_FALSE(result);
    EXPECT_EQ(detail_lane_control_get_last_error(control), DETAIL_LANE_CONTROL_ERROR_IO);
    g_force_fflush_fail = false;

    // Test fclose failure (lines 358-359)
    g_force_fclose_fail = true;
    result = detail_lane_control_write_metadata(control, temp_dir.string().c_str());
    EXPECT_FALSE(result);
    EXPECT_EQ(detail_lane_control_get_last_error(control), DETAIL_LANE_CONTROL_ERROR_IO);
    g_force_fclose_fail = false;

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
    ring_pool_destroy(pool);
    thread_registry_destroy(registry);
}

// Tests for marking_policy.cpp edge cases
TEST_F(CoverageEdgeCaseTest, marking_policy__regex_edge_cases__then_proper_handling) {
    MarkingPolicy* policy = marking_policy_create();
    ASSERT_NE(policy, nullptr);

    // Test case insensitive comparison edge case (lines 45-46)
    AdaMarkingPatternDesc desc1{};
    desc1.target = ADA_MARKING_TARGET_SYMBOL;
    desc1.match = ADA_MARKING_MATCH_LITERAL;
    desc1.case_sensitive = false;
    desc1.pattern = "TeSt";
    marking_policy_add_pattern(policy, &desc1);

    AdaMarkingProbe probe1{};
    probe1.symbol_name = "tEsT";  // Different case
    bool matches = marking_policy_matches(policy, &probe1);
    EXPECT_TRUE(matches);

    // Test empty pattern (lines 110-111, 132-133)
    AdaMarkingPatternDesc desc2{};
    desc2.target = ADA_MARKING_TARGET_SYMBOL;
    desc2.match = ADA_MARKING_MATCH_LITERAL;
    desc2.pattern = "";  // Empty pattern
    MarkingPolicy* policy2 = marking_policy_create();
    marking_policy_add_pattern(policy2, &desc2);

    AdaMarkingProbe probe2{};
    probe2.symbol_name = "anything";
    matches = marking_policy_matches(policy2, &probe2);
    EXPECT_FALSE(matches);  // Empty pattern should not match

    // Test null probe (lines 91-92)
    matches = marking_policy_matches(policy2, nullptr);
    EXPECT_FALSE(matches);

    // Test probe with null symbol_name (lines 91-92)
    AdaMarkingProbe probe3{};
    probe3.symbol_name = nullptr;
    matches = marking_policy_matches(policy2, &probe3);
    EXPECT_FALSE(matches);

    // Test module matching with case insensitive (lines 104-105)
    AdaMarkingPatternDesc desc3{};
    desc3.target = ADA_MARKING_TARGET_SYMBOL;
    desc3.match = ADA_MARKING_MATCH_LITERAL;
    desc3.case_sensitive = false;
    desc3.pattern = "test";
    desc3.module_name = "MyModule";
    MarkingPolicy* policy3 = marking_policy_create();
    marking_policy_add_pattern(policy3, &desc3);

    AdaMarkingProbe probe4{};
    probe4.symbol_name = "test";
    probe4.module_name = "mymodule";  // Different case
    matches = marking_policy_matches(policy3, &probe4);
    EXPECT_TRUE(matches);

    // Test regex without compiled pattern (lines 121-122, 140-141)
    AdaMarkingPatternDesc desc4{};
    desc4.target = ADA_MARKING_TARGET_MESSAGE;
    desc4.match = ADA_MARKING_MATCH_REGEX;
    desc4.pattern = "[invalid(regex";  // Invalid regex that won't compile
    MarkingPolicy* policy4 = marking_policy_create();
    marking_policy_add_pattern(policy4, &desc4);

    AdaMarkingProbe probe5{};
    probe5.message = "test message";
    matches = marking_policy_matches(policy4, &probe5);
    EXPECT_FALSE(matches);  // Should fall back to literal matching

    // Test default case in rule_matches (line 152)
    // This requires creating a pattern with invalid target
    AdaMarkingPatternDesc desc5{};
    desc5.target = (AdaMarkingTarget)999;  // Invalid target
    desc5.pattern = "test";
    MarkingPolicy* policy5 = marking_policy_create();
    marking_policy_add_pattern(policy5, &desc5);

    AdaMarkingProbe probe6{};
    probe6.symbol_name = "test";
    matches = marking_policy_matches(policy5, &probe6);
    EXPECT_FALSE(matches);

    // Cleanup
    marking_policy_destroy(policy);
    marking_policy_destroy(policy2);
    marking_policy_destroy(policy3);
    marking_policy_destroy(policy4);
    marking_policy_destroy(policy5);
}

// Test for marking_policy_from_triggers edge cases (lines 188-189, 200-201)
TEST_F(CoverageEdgeCaseTest, marking_policy_from_triggers__edge_cases__then_handled) {
    // Test null triggers (lines 188-189 path through line 191)
    MarkingPolicy* policy1 = marking_policy_from_triggers(nullptr);
    EXPECT_NE(policy1, nullptr);  // Should return empty policy
    marking_policy_destroy(policy1);

    // Test triggers with null entries
    TriggerList triggers{};
    triggers.count = 1;
    triggers.entries = nullptr;
    MarkingPolicy* policy2 = marking_policy_from_triggers(&triggers);
    EXPECT_NE(policy2, nullptr);  // Should return empty policy
    marking_policy_destroy(policy2);

    // Test trigger with empty symbol_name (lines 200-201)
    TriggerDefinition trig{};
    trig.type = TRIGGER_TYPE_SYMBOL;
    trig.symbol_name = "";  // Empty string
    trig.module_name = nullptr;
    trig.is_regex = false;
    trig.case_sensitive = true;

    TriggerList triggers2{};
    triggers2.count = 1;
    triggers2.entries = &trig;

    MarkingPolicy* policy3 = marking_policy_from_triggers(&triggers2);
    EXPECT_NE(policy3, nullptr);

    // The trigger with empty symbol_name should be skipped
    AdaMarkingProbe probe{};
    probe.symbol_name = "";
    bool matches = marking_policy_matches(policy3, &probe);
    EXPECT_FALSE(matches);

    marking_policy_destroy(policy3);

    // Test trigger with null symbol_name
    TriggerDefinition trig2{};
    trig2.type = TRIGGER_TYPE_SYMBOL;
    trig2.symbol_name = nullptr;  // Null

    TriggerList triggers3{};
    triggers3.count = 1;
    triggers3.entries = &trig2;

    MarkingPolicy* policy4 = marking_policy_from_triggers(&triggers3);
    EXPECT_NE(policy4, nullptr);
    marking_policy_destroy(policy4);
}

// Test exact match for symbol with case sensitive=false (lines 115-116)
TEST_F(CoverageEdgeCaseTest, marking_policy__literal_case_sensitive_symbol__then_exact_match) {
    MarkingPolicy* policy = marking_policy_create();
    ASSERT_NE(policy, nullptr);

    AdaMarkingPatternDesc desc{};
    desc.target = ADA_MARKING_TARGET_SYMBOL;
    desc.match = ADA_MARKING_MATCH_LITERAL;
    desc.case_sensitive = true;  // Case sensitive
    desc.pattern = "exactMatch";

    marking_policy_add_pattern(policy, &desc);

    // Test exact match
    AdaMarkingProbe probe1{};
    probe1.symbol_name = "exactMatch";
    EXPECT_TRUE(marking_policy_matches(policy, &probe1));

    // Test non-match due to case
    AdaMarkingProbe probe2{};
    probe2.symbol_name = "ExactMatch";  // Different case
    EXPECT_FALSE(marking_policy_matches(policy, &probe2));

    // Test non-match due to different string
    AdaMarkingProbe probe3{};
    probe3.symbol_name = "different";
    EXPECT_FALSE(marking_policy_matches(policy, &probe3));

    marking_policy_destroy(policy);
}

}  // namespace