#include <gtest/gtest.h>

#include <atomic>
#include <new>
#include <string>
#include <vector>

#include <tracer_backend/selective_persistence/marking_policy.h>
#include <tracer_backend/cli_parser.h>
static std::atomic<bool> g_fail_policy_nothrow{false};

static void fail_next_policy_allocation() {
    g_fail_policy_nothrow.store(true, std::memory_order_release);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (g_fail_policy_nothrow.exchange(false, std::memory_order_acq_rel)) {
        return nullptr;
    }
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (g_fail_policy_nothrow.exchange(false, std::memory_order_acq_rel)) {
        return nullptr;
    }
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete[](ptr);
}


namespace {

AdaMarkingProbe make_probe(const char* message) {
    AdaMarkingProbe probe{};
    probe.message = message;
    return probe;
}

AdaMarkingPatternDesc literal_pattern(const char* pattern, bool case_sensitive) {
    AdaMarkingPatternDesc desc{};
    desc.target = ADA_MARKING_TARGET_MESSAGE;
    desc.match = ADA_MARKING_MATCH_LITERAL;
    desc.case_sensitive = case_sensitive;
    desc.pattern = pattern;
    return desc;
}

AdaMarkingProbe make_symbol_probe(const char* symbol, const char* module) {
    AdaMarkingProbe probe{};
    probe.symbol_name = symbol;
    probe.module_name = module;
    return probe;
}

AdaMarkingPatternDesc symbol_pattern(const char* pattern, bool case_sensitive, bool regex, const char* module) {
    AdaMarkingPatternDesc desc{};
    desc.target = ADA_MARKING_TARGET_SYMBOL;
    desc.match = regex ? ADA_MARKING_MATCH_REGEX : ADA_MARKING_MATCH_LITERAL;
    desc.case_sensitive = case_sensitive;
    desc.pattern = pattern;
    desc.module_name = module;
    return desc;
}

AdaMarkingPatternDesc regex_pattern(const char* pattern, bool case_sensitive) {
    AdaMarkingPatternDesc desc{};
    desc.target = ADA_MARKING_TARGET_MESSAGE;
    desc.match = ADA_MARKING_MATCH_REGEX;
    desc.case_sensitive = case_sensitive;
    desc.pattern = pattern;
    return desc;
}

}  // namespace

TEST(MarkingPolicyTest, literal_pattern__exact_match__then_detected) {
    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("ERROR: Connection failed");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, literal_pattern__case_sensitive__then_no_match) {
    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", true) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("error: Connection failed");
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, literal_pattern__case_insensitive__then_matches) {
    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("error: Connection failed");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, regex_pattern__valid_regex__then_matches) {
    AdaMarkingPatternDesc patterns[] = { regex_pattern("ERROR|FATAL|CRITICAL", true) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("CRITICAL: System overload");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, regex_pattern__invalid_regex__then_falls_back_to_literal) {
    AdaMarkingPatternDesc patterns[] = { regex_pattern("[invalid_regex", true) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("[invalid_regex found");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, regex_pattern__complex_pattern__then_matches_correctly) {
    AdaMarkingPatternDesc patterns[] = { regex_pattern("\\b(error|warning)\\s+\\d+\\b", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("Found error 404 in response");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, multiple_patterns__any_matches__then_detected) {
    AdaMarkingPatternDesc patterns[] = {
        literal_pattern("ERROR", false),
        literal_pattern("WARNING", false),
        literal_pattern("FATAL", false),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 3);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("WARNING: Low memory");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, multiple_patterns__none_match__then_not_detected) {
    AdaMarkingPatternDesc patterns[] = {
        literal_pattern("ERROR", false),
        literal_pattern("WARNING", false),
        literal_pattern("FATAL", false),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 3);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_probe("INFO: System starting");
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, default_state__disabled__then_no_match) {
    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);

    EXPECT_FALSE(marking_policy_is_enabled(policy));
    AdaMarkingProbe probe = make_probe("ERROR message");
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_set_enabled(policy, true);
    EXPECT_TRUE(marking_policy_is_enabled(policy));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, null_inputs__then_safe_defaults) {
    EXPECT_FALSE(marking_policy_match(nullptr, nullptr));
    AdaMarkingProbe noop_probe = make_probe("noop");
    EXPECT_FALSE(marking_policy_match(nullptr, &noop_probe));

    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);

    EXPECT_FALSE(marking_policy_match(policy, nullptr));
    EXPECT_EQ(marking_policy_pattern_count(nullptr), 0u);

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, allocation_failure__nothrow_new_returns_null__then_create_fails) {
    AdaMarkingPatternDesc patterns[] = { literal_pattern("ERROR", false) };
    fail_next_policy_allocation();
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    EXPECT_EQ(policy, nullptr);
}

TEST(MarkingPolicyTest, empty_pattern__skipped__then_pattern_count_zero) {
    AdaMarkingPatternDesc patterns[] = {
        literal_pattern("", false),
        literal_pattern(nullptr, false),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 2);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(marking_policy_pattern_count(policy), 0u);
    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, symbol_pattern__module_case_insensitive__then_detected) {
    AdaMarkingPatternDesc patterns[] = {
        symbol_pattern("TargetFunction", false, false, "CoreModule"),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_symbol_probe("targetfunction", "coremodule");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, symbol_pattern__probe_missing_module__then_not_detected) {
    AdaMarkingPatternDesc patterns[] = {
        symbol_pattern("TargetFunction", true, false, "CoreModule"),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_symbol_probe("TargetFunction", nullptr);
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, symbol_pattern__regex_case_insensitive__then_detected) {
    AdaMarkingPatternDesc patterns[] = {
        symbol_pattern("Widget::Do.*", false, true, "ModuleAlpha"),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_symbol_probe("widget::domagic", "modulealpha");
    EXPECT_TRUE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, regex_pattern__probe_without_message__then_no_match) {
    AdaMarkingPatternDesc patterns[] = { regex_pattern("ERROR", false) };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe{};
    probe.message = nullptr;
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, symbol_pattern__module_mismatch__then_not_detected) {
    AdaMarkingPatternDesc patterns[] = {
        symbol_pattern("TargetFunction", true, false, "CoreModule"),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe probe = make_symbol_probe("TargetFunction", "OtherModule");
    EXPECT_FALSE(marking_policy_match(policy, &probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, pattern_count__reports_expected) {
    AdaMarkingPatternDesc patterns[] = {
        literal_pattern("ERROR", false),
        symbol_pattern("Func", true, false, nullptr),
    };
    MarkingPolicy* policy = marking_policy_create(patterns, 2);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(marking_policy_pattern_count(policy), 2u);
    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, triggers__mixed_entries__then_rules_generated) {
    std::vector<std::string> symbols = { "alpha", "beta" };
    std::vector<std::string> modules = { "Core", "" };

    std::vector<TriggerDefinition> defs(3);
    defs[0].type = TRIGGER_TYPE_SYMBOL;
    defs[0].symbol_name = symbols[0].data();
    defs[0].module_name = modules[0].data();
    defs[0].case_sensitive = false;
    defs[0].is_regex = false;

    defs[1].type = TRIGGER_TYPE_SYMBOL;
    defs[1].symbol_name = symbols[1].data();
    defs[1].module_name = nullptr;
    defs[1].case_sensitive = true;
    defs[1].is_regex = true;

    defs[2].type = TRIGGER_TYPE_TIME; // ignored

    TriggerList list{};
    list.entries = defs.data();
    list.count = defs.size();

    MarkingPolicy* policy = marking_policy_from_triggers(&list);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(marking_policy_pattern_count(policy), 2u);
    marking_policy_set_enabled(policy, true);

    AdaMarkingProbe literal_probe = make_symbol_probe("alpha", "core");
    EXPECT_TRUE(marking_policy_match(policy, &literal_probe));

    AdaMarkingProbe regex_probe = make_symbol_probe("beta_extra", nullptr);
    EXPECT_TRUE(marking_policy_match(policy, &regex_probe));

    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, from_triggers__null_list__then_empty_policy) {
    MarkingPolicy* policy = marking_policy_from_triggers(nullptr);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(marking_policy_pattern_count(policy), 0u);
    EXPECT_FALSE(marking_policy_is_enabled(policy));
    marking_policy_destroy(policy);
}

TEST(MarkingPolicyTest, create__null_patterns__then_empty_policy) {
    MarkingPolicy* policy = marking_policy_create(nullptr, 5);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(marking_policy_pattern_count(policy), 0u);
    EXPECT_FALSE(marking_policy_match(policy, nullptr));
    marking_policy_destroy(policy);
}
