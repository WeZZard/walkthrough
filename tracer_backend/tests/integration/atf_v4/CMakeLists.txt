add_executable(test_atf_v4_writer_integration
    test_atf_v4_writer_integration.cpp
)

target_include_directories(test_atf_v4_writer_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_atf_v4_writer_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_atf_writer
        tracer_drain_thread
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_atf_v4_writer_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration"
)

install(TARGETS
    test_atf_v4_writer_integration
    RUNTIME DESTINATION bin
)
