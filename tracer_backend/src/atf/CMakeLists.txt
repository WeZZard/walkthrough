set(ATF_V4_WRITER_SOURCES
    atf_v4_writer.c
    trace_schema.pb-c.c
)

add_library(tracer_atf_writer STATIC ${ATF_V4_WRITER_SOURCES})

target_include_directories(tracer_atf_writer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tracer_atf_writer
    PUBLIC
        c_std_11
)

set_target_properties(tracer_atf_writer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Provide include directory for generated protobuf headers
target_include_directories(tracer_atf_writer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tracer_atf_writer
    PUBLIC
        tracer_utils
)
