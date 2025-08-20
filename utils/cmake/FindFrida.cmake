# FindFrida.cmake
# Locate Frida SDK components in the third_parties directory
#
# This module defines:
#   FRIDA_FOUND - System has Frida SDK
#   FRIDA_CORE_INCLUDE_DIR - Frida Core include directory
#   FRIDA_CORE_LIBRARY - Frida Core library
#   FRIDA_GUM_INCLUDE_DIR - Frida Gum include directory
#   FRIDA_GUM_LIBRARY - Frida Gum library
#   FRIDA_VERSION - Frida version

# Get the directory of this cmake file
get_filename_component(FRIDA_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(PROJECT_ROOT_DIR "${FRIDA_CMAKE_DIR}/../.." ABSOLUTE)

# Set the third_parties directory
set(THIRD_PARTIES_DIR "${PROJECT_ROOT_DIR}/third_parties")

# Find Frida Core
find_path(FRIDA_CORE_INCLUDE_DIR
    NAMES frida-core.h
    PATHS "${THIRD_PARTIES_DIR}/frida-core"
    NO_DEFAULT_PATH
)

find_library(FRIDA_CORE_LIBRARY
    NAMES frida-core
    PATHS "${THIRD_PARTIES_DIR}/frida-core"
    NO_DEFAULT_PATH
)

# Find Frida Gum
find_path(FRIDA_GUM_INCLUDE_DIR
    NAMES frida-gum.h
    PATHS "${THIRD_PARTIES_DIR}/frida-gum"
    NO_DEFAULT_PATH
)

find_library(FRIDA_GUM_LIBRARY
    NAMES frida-gum
    PATHS "${THIRD_PARTIES_DIR}/frida-gum"
    NO_DEFAULT_PATH
)

# Check if we found everything
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Frida
    REQUIRED_VARS
        FRIDA_CORE_INCLUDE_DIR
        FRIDA_CORE_LIBRARY
        FRIDA_GUM_INCLUDE_DIR
        FRIDA_GUM_LIBRARY
)

# Create imported targets
if(FRIDA_FOUND AND NOT TARGET Frida::Core)
    add_library(Frida::Core STATIC IMPORTED)
    set_target_properties(Frida::Core PROPERTIES
        IMPORTED_LOCATION "${FRIDA_CORE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FRIDA_CORE_INCLUDE_DIR}"
    )
    
    # Platform-specific linking requirements for Frida Core
    if(APPLE)
        set_target_properties(Frida::Core PROPERTIES
            INTERFACE_LINK_LIBRARIES "-framework Foundation -framework AppKit -lbsm -lresolv"
        )
    elseif(UNIX)
        set_target_properties(Frida::Core PROPERTIES
            INTERFACE_LINK_LIBRARIES "-lpthread -ldl"
        )
    endif()
endif()

if(FRIDA_FOUND AND NOT TARGET Frida::Gum)
    add_library(Frida::Gum STATIC IMPORTED)
    set_target_properties(Frida::Gum PROPERTIES
        IMPORTED_LOCATION "${FRIDA_GUM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FRIDA_GUM_INCLUDE_DIR}"
    )
    
    # Platform-specific linking requirements for Frida Gum
    if(APPLE)
        set_target_properties(Frida::Gum PROPERTIES
            INTERFACE_LINK_LIBRARIES "-framework Foundation"
        )
    elseif(UNIX)
        set_target_properties(Frida::Gum PROPERTIES
            INTERFACE_LINK_LIBRARIES "-lpthread -ldl"
        )
    endif()
endif()

# Try to extract version from header if possible
if(FRIDA_CORE_INCLUDE_DIR AND EXISTS "${FRIDA_CORE_INCLUDE_DIR}/frida-core.h")
    file(STRINGS "${FRIDA_CORE_INCLUDE_DIR}/frida-core.h" frida_version_line
        REGEX "^#define FRIDA_VERSION")
    if(frida_version_line)
        string(REGEX REPLACE "^#define FRIDA_VERSION \"([0-9.]+)\".*" "\\1"
            FRIDA_VERSION "${frida_version_line}")
    else()
        set(FRIDA_VERSION "unknown")
    endif()
endif()

mark_as_advanced(
    FRIDA_CORE_INCLUDE_DIR
    FRIDA_CORE_LIBRARY
    FRIDA_GUM_INCLUDE_DIR
    FRIDA_GUM_LIBRARY
)