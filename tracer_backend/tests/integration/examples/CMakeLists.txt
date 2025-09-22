add_executable(test_examples_integration
    test_examples_integration.cpp
)

target_link_libraries(test_examples_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
)

target_include_directories(test_examples_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_examples_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration"
)
