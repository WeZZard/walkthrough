// Comprehensive hooks planning utilities. These helpers are designed to be
// Frida-agnostic and provide filtered symbol plans and function id assignment
// using HookRegistry and the exclude list.

#ifndef ADA_COMPREHENSIVE_HOOKS_H
#define ADA_COMPREHENSIVE_HOOKS_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

extern "C" {
#include <tracer_backend/agent/exclude_list.h>
}

namespace ada {
namespace agent {

class HookRegistry;

// Result entry for a planned hook (symbol -> function_id)
struct HookPlanEntry {
    std::string symbol;
    uint64_t function_id;
};

// Plan hooks for a single module, given its exported symbol names.
// - module_path: the path or logical name of the DSO
// - exports: list of exported symbol names
// - excludes: optional exclude list (can be nullptr)
// - registry: used to assign stable function ids
// Returns: vector of HookPlanEntry for symbols that are not excluded.
std::vector<HookPlanEntry> plan_module_hooks(
    const std::string& module_path,
    const std::vector<std::string>& exports,
    AdaExcludeList* excludes,
    HookRegistry& registry);

// Plan hooks for main binary and a set of DSOs.
// - main_exports: export names from the main binary
// - dso_names: names/paths of DSOs
// - dso_exports: parallel vector where dso_exports[i] contains exports for dso_names[i]
// Returns: flattened set of hook plan entries for all modules.
std::vector<HookPlanEntry> plan_comprehensive_hooks(
    const std::vector<std::string>& main_exports,
    const std::vector<std::string>& dso_names,
    const std::vector<std::vector<std::string>>& dso_exports,
    AdaExcludeList* excludes,
    HookRegistry& registry);

} // namespace agent
} // namespace ada

#endif // ADA_COMPREHENSIVE_HOOKS_H

