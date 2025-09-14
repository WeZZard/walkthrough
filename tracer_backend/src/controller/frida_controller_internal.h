#ifndef FRIDA_CONTROLLER_INTERNAL_H
#define FRIDA_CONTROLLER_INTERNAL_H

#include <memory>
#include <atomic>
#include <string>
#include <cstdint>
#include <thread>

extern "C" {
#include <frida-core.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/thread_registry.h>
}

// Forward declare internal C++ types
namespace ada {
namespace internal {
    class RingBuffer;
    class ThreadRegistry;
} // namespace internal
} // namespace ada

namespace ada {
namespace internal {

// ============================================================================
// FridaController Implementation Class
// ============================================================================

class FridaController {
public:
    // Constructor/Destructor
    explicit FridaController(const std::string& output_dir);
    ~FridaController();
    
    // Disable copy/move
    FridaController(const FridaController&) = delete;
    FridaController& operator=(const FridaController&) = delete;
    FridaController(FridaController&&) = delete;
    FridaController& operator=(FridaController&&) = delete;
    
    // Process management
    int spawn_suspended(const char* path, char* const argv[], uint32_t* out_pid);
    int attach(uint32_t pid);
    int detach();
    int resume();
    int pause();
    
    // Agent injection
    int install_hooks();
    int inject_agent(const char* agent_path);
    
    // Flight recorder control
    int arm_trigger(uint32_t pre_roll_ms, uint32_t post_roll_ms);
    int fire_trigger();
    int disarm_trigger();
    
    // State query
    ProcessState get_state() const { return state_; }
    FlightRecorderState get_flight_state() const;
    TracerStats get_stats() const { return stats_; }
    
private:
    // Spawn methods
    enum class SpawnMethod {
        None,
        Frida
    };
    
    // RAII wrappers for Frida objects
    struct FridaDeleter {
        void operator()(gpointer obj) { if (obj) frida_unref(obj); }
    };
    
    template<typename T>
    using frida_ptr = std::unique_ptr<T, FridaDeleter>;
    
    // RAII wrapper for shared memory
    struct SharedMemoryDeleter {
        void operator()(SharedMemoryRef ref) { 
            if (ref) shared_memory_destroy(ref); 
        }
    };
    using shared_memory_ptr = std::unique_ptr<__SharedMemory, SharedMemoryDeleter>;
    
    // Member functions
    bool initialize_shared_memory();
    bool initialize_ring_buffers();
    void cleanup_frida_objects();
    void drain_thread_main();
    std::string build_shm_name(const char* role, pid_t pid_hint = 0);
    
    // Static callbacks for Frida signals
    static void on_detached_callback(FridaSession* session, 
                                     FridaSessionDetachReason reason,
                                     FridaCrash* crash, 
                                     gpointer user_data);
    static void on_message_callback(FridaScript* script, 
                                    const gchar* message,
                                    GBytes* data, 
                                    gpointer user_data);
    
    // Instance callbacks
    void on_detached(FridaSessionDetachReason reason, FridaCrash* crash);
    void on_message(const gchar* message, GBytes* data);
    
    // Data members
    std::string output_dir_;
    
    // Frida objects (raw pointers managed via RAII)
    FridaDeviceManager* manager_{nullptr};
    FridaDevice* device_{nullptr};
    FridaSession* session_{nullptr};
    FridaScript* script_{nullptr};
    
    // Process management
    guint pid_{0};
    std::atomic<ProcessState> state_{PROCESS_STATE_UNINITIALIZED};
    SpawnMethod spawn_method_{SpawnMethod::None};
    
    // Shared memory
    shared_memory_ptr shm_control_;
    shared_memory_ptr shm_index_;
    shared_memory_ptr shm_detail_;
    shared_memory_ptr shm_registry_;
    ControlBlock* control_block_{nullptr};
    ::ThreadRegistry* registry_{nullptr};
    
    // Ring buffers (using internal C++ classes)
    std::unique_ptr<RingBuffer> index_ring_;
    std::unique_ptr<RingBuffer> detail_ring_;
    
    // Drain thread
    std::unique_ptr<std::thread> drain_thread_;
    std::atomic<bool> drain_running_{false};
    uint32_t drain_ticks_{0};
    
    // Output
    FILE* output_file_{nullptr};
    
    // Statistics
    mutable TracerStats stats_{};
    
    // Event loop
    GMainLoop* main_loop_{nullptr};
    GMainContext* main_context_{nullptr};
    
    // Constants
    static constexpr size_t INDEX_LANE_SIZE = 32 * 1024 * 1024;   // 32MB
    static constexpr size_t DETAIL_LANE_SIZE = 32 * 1024 * 1024;  // 32MB
    static constexpr size_t CONTROL_BLOCK_SIZE = 4096;
};

} // namespace internal
} // namespace ada

#endif // FRIDA_CONTROLLER_INTERNAL_H
