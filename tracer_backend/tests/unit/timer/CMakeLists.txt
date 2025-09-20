# ===========================================
# Timer Unit Tests
# ===========================================

add_executable(test_timer
    test_timer.c
    timer_whitebox.c
)

set_source_files_properties(test_timer.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(timer_whitebox.c PROPERTIES LANGUAGE C)

target_include_directories(test_timer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_timer
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_timer
        Threads::Threads
)

gtest_discover_tests(test_timer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
