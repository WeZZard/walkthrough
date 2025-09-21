#ifndef ADA_SELECTIVE_PERSISTENCE_MARKING_POLICY_H
#define ADA_SELECTIVE_PERSISTENCE_MARKING_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TriggerList; // Forward declaration from cli_parser.h

// Enumeration describing the target field to evaluate for a marking rule.
typedef enum {
    ADA_MARKING_TARGET_SYMBOL = 0,
    ADA_MARKING_TARGET_MESSAGE = 1,
} AdaMarkingTarget;

// Enumeration describing how a pattern should be matched.
typedef enum {
    ADA_MARKING_MATCH_LITERAL = 0,
    ADA_MARKING_MATCH_REGEX = 1,
} AdaMarkingMatch;

// Probe presented to the marking policy when evaluating an event.
typedef struct AdaMarkingProbe {
    const char* symbol_name;   // Optional symbol name for the event
    const char* module_name;   // Optional module/namespace for the event
    const char* message;       // Optional textual payload for the event
} AdaMarkingProbe;

// Pattern description used to construct a marking policy. Strings are copied.
typedef struct AdaMarkingPatternDesc {
    AdaMarkingTarget target;
    AdaMarkingMatch match;
    bool case_sensitive;
    const char* pattern;
    const char* module_name; // Optional; used when target == SYMBOL
} AdaMarkingPatternDesc;

// Opaque marking policy type.
typedef struct MarkingPolicy MarkingPolicy;

// Create a policy from an explicit list of pattern descriptors. Returns NULL on failure.
MarkingPolicy* marking_policy_create(const AdaMarkingPatternDesc* patterns, size_t count);

// Create a policy using CLI trigger definitions. Returns NULL if triggers cannot be mapped.
MarkingPolicy* marking_policy_from_triggers(const struct TriggerList* triggers);

// Destroy a policy created with the factory functions above.
void marking_policy_destroy(MarkingPolicy* policy);

// Enable or disable the policy at runtime.
void marking_policy_set_enabled(MarkingPolicy* policy, bool enabled);

// Return whether the policy is currently enabled.
bool marking_policy_is_enabled(const MarkingPolicy* policy);

// Evaluate a probe against the policy. Returns true if any pattern matches.
bool marking_policy_match(const MarkingPolicy* policy, const AdaMarkingProbe* probe);

// Return the number of patterns stored in the policy.
size_t marking_policy_pattern_count(const MarkingPolicy* policy);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADA_SELECTIVE_PERSISTENCE_MARKING_POLICY_H
