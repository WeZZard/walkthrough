# ===========================================
# Duration Timer Module
# ===========================================

add_library(tracer_timer STATIC
    timer.c
)

set_target_properties(tracer_timer PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

target_include_directories(tracer_timer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tracer_timer
    PUBLIC
        Threads::Threads
)
