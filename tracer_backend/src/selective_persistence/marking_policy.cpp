#include <tracer_backend/selective_persistence/marking_policy.h>

#include <new>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <tracer_backend/cli_parser.h>

namespace {

struct MarkingRule {
    AdaMarkingTarget target{ADA_MARKING_TARGET_MESSAGE};
    AdaMarkingMatch match{ADA_MARKING_MATCH_LITERAL};
    bool case_sensitive{true};
    std::string pattern;
    std::string module; // Optional qualifier for symbol target
    std::unique_ptr<std::regex> compiled;
};

struct MarkingPolicyImpl {
    std::vector<MarkingRule> rules;
    std::atomic<bool> enabled{false};
};

std::string to_lower_copy(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

bool equals_case_insensitive(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

bool contains_literal(std::string_view haystack, std::string_view needle, bool case_sensitive) {
    if (needle.empty()) return false;
    if (case_sensitive) {
        return haystack.find(needle) != std::string_view::npos;
    }
    std::string lower_haystack = to_lower_copy(haystack);
    std::string lower_needle = to_lower_copy(needle);
    return lower_haystack.find(lower_needle) != std::string::npos;
}

MarkingRule make_rule_from_desc(const AdaMarkingPatternDesc& desc) {
    MarkingRule rule;
    rule.target = desc.target;
    rule.match = desc.match;
    rule.case_sensitive = desc.case_sensitive;
    if (desc.pattern) {
        rule.pattern = desc.pattern;
    }
    if (desc.module_name) {
        rule.module = desc.module_name;
    }

    if (rule.match == ADA_MARKING_MATCH_REGEX && !rule.pattern.empty()) {
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!rule.case_sensitive) {
            flags = static_cast<std::regex::flag_type>(flags | std::regex::icase);
        }
        try {
            rule.compiled = std::make_unique<std::regex>(rule.pattern, flags);
        } catch (const std::regex_error&) {
            // Fall back to literal matching when pattern is invalid.
            rule.match = ADA_MARKING_MATCH_LITERAL;
            rule.compiled.reset();
        }
    }
    return rule;
}

bool match_symbol_rule(const MarkingRule& rule, const AdaMarkingProbe* probe) {
    if (!probe || !probe->symbol_name) {
        return false;
    }
    if (!rule.module.empty()) {
        if (!probe->module_name) {
            return false;
        }
        std::string_view probe_module{probe->module_name};
        std::string_view rule_module{rule.module};
        if (rule.case_sensitive) {
            if (probe_module != rule_module) {
                return false;
            }
        } else if (!equals_case_insensitive(probe_module, rule_module)) {
            return false;
        }
    }

    std::string_view candidate{probe->symbol_name};
    if (rule.pattern.empty()) {
        return false;
    }

    if (rule.match == ADA_MARKING_MATCH_LITERAL) {
        if (rule.case_sensitive) {
            return candidate == rule.pattern;
        }
        return equals_case_insensitive(candidate, rule.pattern);
    }

    if (!rule.compiled) {
        return false;
    }

    return std::regex_search(candidate.begin(), candidate.end(), *rule.compiled);
}

bool match_message_rule(const MarkingRule& rule, const AdaMarkingProbe* probe) {
    if (!probe || !probe->message) {
        return false;
    }
    if (rule.pattern.empty()) {
        return false;
    }

    std::string_view message{probe->message};
    if (rule.match == ADA_MARKING_MATCH_LITERAL) {
        return contains_literal(message, rule.pattern, rule.case_sensitive);
    }
    if (!rule.compiled) {
        return false;
    }
    return std::regex_search(message.begin(), message.end(), *rule.compiled);
}

bool rule_matches(const MarkingRule& rule, const AdaMarkingProbe* probe) {
    switch (rule.target) {
        case ADA_MARKING_TARGET_SYMBOL:
            return match_symbol_rule(rule, probe);
        case ADA_MARKING_TARGET_MESSAGE:
            return match_message_rule(rule, probe);
        default:
            return false;
    }
}

}  // namespace

struct MarkingPolicy {
    MarkingPolicyImpl impl;
};

MarkingPolicy* marking_policy_create(const AdaMarkingPatternDesc* patterns, size_t count) {
    auto* wrapper = new (std::nothrow) MarkingPolicy();
    if (!wrapper) {
        return nullptr;
    }

    if (patterns && count > 0) {
        wrapper->impl.rules.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const AdaMarkingPatternDesc& desc = patterns[i];
            if (!desc.pattern || desc.pattern[0] == '\0') {
                continue;
            }
            MarkingRule rule = make_rule_from_desc(desc);
            if (!rule.pattern.empty()) {
                wrapper->impl.rules.push_back(std::move(rule));
            }
        }
    }

    return wrapper;
}

MarkingPolicy* marking_policy_from_triggers(const TriggerList* triggers) {
    auto* wrapper = new (std::nothrow) MarkingPolicy();
    if (!wrapper) {
        return nullptr;
    }
    if (!triggers || !triggers->entries) {
        return wrapper;
    }

    for (size_t i = 0; i < triggers->count; ++i) {
        const TriggerDefinition& trig = triggers->entries[i];
        if (trig.type != TRIGGER_TYPE_SYMBOL) {
            continue;
        }
        if (!trig.symbol_name || trig.symbol_name[0] == '\0') {
            continue;
        }

        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = trig.is_regex ? ADA_MARKING_MATCH_REGEX : ADA_MARKING_MATCH_LITERAL;
        desc.case_sensitive = trig.case_sensitive;
        desc.pattern = trig.symbol_name;
        desc.module_name = trig.module_name;

        MarkingRule rule = make_rule_from_desc(desc);
        if (!rule.pattern.empty()) {
            wrapper->impl.rules.push_back(std::move(rule));
        }
    }
    return wrapper;
}

void marking_policy_destroy(MarkingPolicy* policy) {
    delete policy;
}

void marking_policy_set_enabled(MarkingPolicy* policy, bool enabled) {
    if (!policy) return;
    policy->impl.enabled.store(enabled, std::memory_order_release);
}

bool marking_policy_is_enabled(const MarkingPolicy* policy) {
    if (!policy) return false;
    return policy->impl.enabled.load(std::memory_order_acquire);
}

bool marking_policy_match(const MarkingPolicy* policy, const AdaMarkingProbe* probe) {
    if (!policy || !probe) {
        return false;
    }
    if (!policy->impl.enabled.load(std::memory_order_acquire)) {
        return false;
    }

    for (const auto& rule : policy->impl.rules) {
        if (rule_matches(rule, probe)) {
            return true;
        }
    }
    return false;
}

size_t marking_policy_pattern_count(const MarkingPolicy* policy) {
    if (!policy) return 0;
    return policy->impl.rules.size();
}
