set(DOCS_SOURCES
    doc_builder.c
    example_runner.c
    troubleshoot.c
    platform_check.c
)

add_library(tracer_docs STATIC ${DOCS_SOURCES})

target_include_directories(tracer_docs
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tracer_docs
    PUBLIC
        Threads::Threads
)

target_compile_features(tracer_docs PUBLIC c_std_11)
