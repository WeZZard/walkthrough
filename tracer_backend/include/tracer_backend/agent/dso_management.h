// DSO management for dynamic shared objects loaded in the target process.
// Provides a lightweight registry of DSOs and simple interception hooks
// (interception wiring is left to the agent; this utility is Frida-free).

#ifndef ADA_DSO_MANAGEMENT_H
#define ADA_DSO_MANAGEMENT_H

#include <cstdint>
#include <string>
#include <vector>

namespace ada {
namespace agent {

struct DsoInfo {
    std::string path;      // Canonical path of the DSO
    uintptr_t base;        // Image base address (if known, 0 otherwise)
    void* handle;          // dlopen handle (if known, nullptr otherwise)
};

// Thread‑safe DSO registry.
class DsoRegistry {
public:
    DsoRegistry();

    // Add or update a DSO record. If an entry with the same handle or base
    // exists, it will be updated; otherwise it is appended.
    void add(const std::string& path, uintptr_t base, void* handle);

    // Remove a DSO by handle or base. Returns true if removed.
    bool remove_by_handle(void* handle);
    bool remove_by_base(uintptr_t base);

    // Queries (copy out a snapshot to avoid holding the lock for user code).
    std::vector<DsoInfo> list() const;
    bool find_by_handle(void* handle, DsoInfo* out) const;
    bool find_by_base(uintptr_t base, DsoInfo* out) const;

    // Utility: clear registry (tests only)
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<DsoInfo> dsos_;
};

// Global singleton accessor used by agent code.
DsoRegistry& dso_registry();

// Interception glue for dlopen/dlclose. In unit tests these can be called
// directly to simulate DSO arrival/teardown. Runtime agent can wire them to
// actual interceptors.
void dso_on_load(const char* path, void* handle, uintptr_t base);
void dso_on_unload(void* handle, uintptr_t base);

} // namespace agent
} // namespace ada

#endif // ADA_DSO_MANAGEMENT_H

