# ===========================================
# CLI Parser Library
# ===========================================

set(CLI_PARSER_SOURCES
    cli_parser.c
)

add_library(tracer_cli_parser STATIC ${CLI_PARSER_SOURCES})

target_include_directories(tracer_cli_parser
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tracer_cli_parser
    PUBLIC
        c_std_11
)
