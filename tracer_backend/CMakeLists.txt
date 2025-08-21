cmake_minimum_required(VERSION 3.15)
project(tracer_backend VERSION 0.1.0 LANGUAGES C CXX)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add the cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../utils/cmake")

# Find required packages
find_package(Threads REQUIRED)

# Set third_parties directory
set(THIRD_PARTIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_parties")

# Manually specify Frida paths since we have a custom setup
set(FRIDA_CORE_INCLUDE_DIR "${THIRD_PARTIES_DIR}/frida-core")
set(FRIDA_CORE_LIBRARY "${THIRD_PARTIES_DIR}/frida-core/libfrida-core.a")
set(FRIDA_GUM_INCLUDE_DIR "${THIRD_PARTIES_DIR}/frida-gum")
set(FRIDA_GUM_LIBRARY "${THIRD_PARTIES_DIR}/frida-gum/libfrida-gum.a")

# Verify Frida files exist
if(NOT EXISTS ${FRIDA_CORE_LIBRARY})
    message(FATAL_ERROR "Frida Core library not found. Run utils/init_third_parties.sh first.")
endif()
if(NOT EXISTS ${FRIDA_GUM_LIBRARY})
    message(FATAL_ERROR "Frida Gum library not found. Run utils/init_third_parties.sh first.")
endif()

message(STATUS "Found Frida Core: ${FRIDA_CORE_LIBRARY}")
message(STATUS "Found Frida Gum: ${FRIDA_GUM_LIBRARY}")

# ===========================================
# Shared utilities library
# ===========================================
add_library(tracer_utils STATIC
    src/shared_memory.c
    src/ring_buffer.c
)

target_include_directories(tracer_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tracer_utils
    PUBLIC
        Threads::Threads
)

# ===========================================
# Controller Library (runs in tracer process)
# ===========================================
add_library(tracer_controller STATIC
    src/frida_controller.c
)

target_include_directories(tracer_controller
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(tracer_controller
    PUBLIC
        tracer_utils
        ${FRIDA_CORE_LIBRARY}
        Threads::Threads
)

# Platform-specific linking for Frida Core
if(APPLE)
    target_link_libraries(tracer_controller
        PUBLIC
            "-framework Foundation"
            "-framework AppKit"
            "-framework IOKit"
            "-framework Security"
            "-lbsm"
            "-lresolv"
    )
elseif(UNIX)
    target_link_libraries(tracer_controller
        PUBLIC
            pthread
            dl
    )
endif()

# ===========================================
# Agent Library (injected into target process)
# ===========================================
add_library(frida_agent SHARED
    src/frida_agent.c
)

target_include_directories(frida_agent
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_GUM_INCLUDE_DIR}
)

target_link_libraries(frida_agent
    PUBLIC
        tracer_utils
        ${FRIDA_GUM_LIBRARY}
        Threads::Threads
)

# Platform-specific linking for Frida Gum
if(APPLE)
    target_link_libraries(frida_agent
        PUBLIC
            "-framework Foundation"
            "-lresolv"
    )
    
    # Set proper rpath
    set_target_properties(frida_agent PROPERTIES
        INSTALL_RPATH "@loader_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(UNIX)
    target_link_libraries(frida_agent
        PUBLIC
            pthread
            dl
            m
    )
    
    set_target_properties(frida_agent PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Export the agent entry point
set_target_properties(frida_agent PROPERTIES
    COMPILE_FLAGS "-fvisibility=hidden"
)

# ===========================================
# Test Fixtures
# ===========================================
add_executable(test_cli
    tests/fixtures/test_cli.c
)

target_link_libraries(test_cli
    PRIVATE
        m
)

add_executable(test_runloop
    tests/fixtures/test_runloop.c
)

if(APPLE)
    target_link_libraries(test_runloop
        PRIVATE
            "-framework CoreFoundation"
    )
endif()

# ===========================================
# Main tracer executable
# ===========================================
add_executable(tracer_poc
    src/main.c
)

target_include_directories(tracer_poc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(tracer_poc
    PRIVATE
        tracer_controller
        tracer_utils
)

# ===========================================
# Tests
# ===========================================
enable_testing()

# Unit tests for shared memory
add_executable(test_shared_memory
    tests/unit/test_shared_memory.c
)

target_include_directories(test_shared_memory
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_shared_memory
    PRIVATE
        tracer_utils
)

add_test(NAME test_shared_memory COMMAND test_shared_memory)

# Unit tests for ring buffer
add_executable(test_ring_buffer
    tests/unit/test_ring_buffer.c
)

target_include_directories(test_ring_buffer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_ring_buffer
    PRIVATE
        tracer_utils
        Threads::Threads
)

add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

# Unit tests for ring buffer attach
add_executable(test_ring_buffer_attach
    tests/unit/test_ring_buffer_attach.c
)

target_include_directories(test_ring_buffer_attach
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_ring_buffer_attach
    PRIVATE
        tracer_utils
        Threads::Threads
)

add_test(NAME test_ring_buffer_attach COMMAND test_ring_buffer_attach)

# Unit tests for spawn method tracking
add_executable(test_spawn_method
    tests/unit/test_spawn_method.c
)

target_include_directories(test_spawn_method
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(test_spawn_method
    PRIVATE
        tracer_controller
        tracer_utils
)

add_test(NAME test_spawn_method COMMAND test_spawn_method)

# Integration test
add_executable(test_integration
    tests/integration/test_integration.c
)

target_include_directories(test_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(test_integration
    PRIVATE
        tracer_controller
        tracer_utils
)

add_test(NAME test_integration COMMAND test_integration)

# P0 fixes integration test
add_executable(test_p0_fixes
    tests/integration/test_p0_fixes.c
)

target_include_directories(test_p0_fixes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FRIDA_CORE_INCLUDE_DIR}
)

target_link_libraries(test_p0_fixes
    PRIVATE
        tracer_controller
        tracer_utils
        ${CMAKE_DL_LIBS}
)

add_test(NAME test_p0_fixes COMMAND test_p0_fixes)

# ===========================================
# Installation
# ===========================================
install(TARGETS tracer_controller tracer_utils frida_agent tracer_poc test_cli test_runloop
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ===========================================
# Summary
# ===========================================
message(STATUS "")
message(STATUS "tracer-backend configuration summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  C compiler:     ${CMAKE_C_COMPILER}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Components to build:")
message(STATUS "  Controller library: tracer_controller")
message(STATUS "  Agent library:      frida_agent")
message(STATUS "  Test CLI:          test_cli")
message(STATUS "  Test RunLoop:      test_runloop")
message(STATUS "  POC executable:    tracer_poc")
message(STATUS "")