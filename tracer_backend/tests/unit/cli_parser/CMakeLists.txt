# ===========================================
# CLI Parser Unit Tests
# ===========================================

add_executable(test_cli_parser_mode_detection
    cli_parser_mode_tests.cpp
)

target_link_libraries(test_cli_parser_mode_detection
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_cli_parser
)

target_include_directories(test_cli_parser_mode_detection
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_cli_parser_mode_detection
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

add_executable(test_cli_parser_args
    test_cli_parser_args.cpp
)

target_link_libraries(test_cli_parser_args
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_cli_parser
)

target_include_directories(test_cli_parser_args
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_cli_parser_args
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

add_executable(test_cli_parser_comprehensive
    test_cli_parser_comprehensive.cpp
)

target_link_libraries(test_cli_parser_comprehensive
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
)

target_include_directories(test_cli_parser_comprehensive
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_cli_parser_comprehensive
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

