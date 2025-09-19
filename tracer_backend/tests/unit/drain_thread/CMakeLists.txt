# ===========================================
# Drain Thread Unit Tests
# ===========================================

add_executable(test_drain_thread
    test_drain_thread.cpp
)

target_include_directories(test_drain_thread
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_drain_thread
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_drain_thread
        tracer_atf_writer
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_drain_thread
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_drain_thread
    RUNTIME DESTINATION bin
)

add_executable(test_per_thread_drain
    test_per_thread_drain.cpp
)

target_include_directories(test_per_thread_drain
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_per_thread_drain
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_drain_thread
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_per_thread_drain
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_per_thread_drain
    RUNTIME DESTINATION bin
)
