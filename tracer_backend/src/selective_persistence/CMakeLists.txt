# ===========================================
# Selective Persistence Library
# ===========================================

set(SELECTIVE_PERSISTENCE_SOURCES
    detail_lane_control.cpp
    metrics.cpp
    marking_policy.cpp
    persistence_window.cpp
)

add_library(tracer_selective_persistence STATIC ${SELECTIVE_PERSISTENCE_SOURCES})

target_include_directories(tracer_selective_persistence
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/utils
)

target_link_libraries(tracer_selective_persistence
    PUBLIC
        tracer_utils
        Threads::Threads
)

target_compile_features(tracer_selective_persistence
    PUBLIC
        c_std_11
        cxx_std_17
)

set_target_properties(tracer_selective_persistence PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
