# ===========================================
# Timer Integration Tests
# ===========================================

add_executable(test_timer_integration
    test_timer_integration.cpp
)

set_source_files_properties(test_timer_integration.cpp PROPERTIES LANGUAGE CXX)

target_include_directories(test_timer_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h if needed
)

target_link_libraries(test_timer_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_timer
        Threads::Threads
)

gtest_discover_tests(test_timer_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration"
)

install(TARGETS
    test_timer_integration
    RUNTIME DESTINATION bin
)
