# ===========================================
# Registry Benchmark Tests
# ===========================================

add_executable(test_registry_bench
    test_registry_bench.cpp
)

target_include_directories(test_registry_bench
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)

target_link_libraries(test_registry_bench
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_registry_bench
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "bench"
)

install(TARGETS
    test_registry_bench
    RUNTIME DESTINATION bin
)

