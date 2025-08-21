# Build configuration for third-party Frida examples
cmake_minimum_required(VERSION 3.10)
project(frida_examples)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set paths to Frida libraries
set(FRIDA_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frida-core")
set(FRIDA_GUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frida-gum")

# Frida Gum Example
add_executable(frida_gum_example
    ${FRIDA_GUM_DIR}/frida-gum-example.c
)

target_include_directories(frida_gum_example PRIVATE
    ${FRIDA_GUM_DIR}
)

target_link_libraries(frida_gum_example
    ${FRIDA_GUM_DIR}/libfrida-gum.a
    resolv
    m
)

# Frida Core Example
add_executable(frida_core_example
    ${FRIDA_CORE_DIR}/frida-core-example.c
)

target_include_directories(frida_core_example PRIVATE
    ${FRIDA_CORE_DIR}
)

target_link_libraries(frida_core_example
    ${FRIDA_CORE_DIR}/libfrida-core.a
    bsm
    dl
    m
    resolv
)

# Platform-specific linking
if(APPLE)
    target_link_libraries(frida_core_example
        "-framework Foundation"
        "-framework AppKit"
        "-framework Security"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# Install to predictable location
install(TARGETS frida_gum_example frida_core_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)