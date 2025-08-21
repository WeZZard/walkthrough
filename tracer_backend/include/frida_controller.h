#ifndef FRIDA_CONTROLLER_H
#define FRIDA_CONTROLLER_H

#include "tracer_types.h"
#include <frida-core.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct FridaController FridaController;

// Controller lifecycle
FridaController* frida_controller_create(const char* output_dir);
void frida_controller_destroy(FridaController* controller);

// Process management
int frida_controller_spawn_suspended(FridaController* controller, 
                                     const char* path, 
                                     char* const argv[],
                                     uint32_t* out_pid);
int frida_controller_attach(FridaController* controller, uint32_t pid);
int frida_controller_detach(FridaController* controller);
int frida_controller_resume(FridaController* controller);
int frida_controller_pause(FridaController* controller);

// Hook installation
int frida_controller_install_hooks(FridaController* controller);
int frida_controller_inject_agent(FridaController* controller, const char* agent_path);

// Flight recorder control
int frida_controller_arm_trigger(FridaController* controller,
                                 uint32_t pre_roll_ms,
                                 uint32_t post_roll_ms);
int frida_controller_fire_trigger(FridaController* controller);
int frida_controller_disarm_trigger(FridaController* controller);

// Statistics
TracerStats frida_controller_get_stats(FridaController* controller);

// State query
ProcessState frida_controller_get_state(FridaController* controller);
FlightRecorderState frida_controller_get_flight_state(FridaController* controller);

#ifdef __cplusplus
}
#endif

#endif // FRIDA_CONTROLLER_H