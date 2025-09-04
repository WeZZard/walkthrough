# ============================================================================
# Interface Validation Library
# ============================================================================
# This library provides skeleton implementations to validate that all
# interfaces compile and link correctly. Real implementations go in
# their respective modules (utils, controller, etc.)

# Interface skeleton library - C implementation for FFI compatibility
add_library(tracer_interfaces STATIC
    thread_registry_skeleton.c
)

# Include paths
target_include_directories(tracer_interfaces
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Link dependencies
target_link_libraries(tracer_interfaces
    PUBLIC
        Threads::Threads
)

# Set C standard for this target
set_target_properties(tracer_interfaces PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Export for other modules to use
set(TRACER_INTERFACES_LIB tracer_interfaces PARENT_SCOPE)