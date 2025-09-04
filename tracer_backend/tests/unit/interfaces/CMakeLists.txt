# ============================================================================
# Interface Unit Tests
# ============================================================================

# Interface compilation test
add_executable(test_interface_compilation
    test_interface_compilation.cpp
)

target_link_libraries(test_interface_compilation
    PRIVATE
        test_main
        tracer_interfaces  # Links the skeleton implementation
        GTest::gtest
        GTest::gmock
        Threads::Threads
)

target_include_directories(test_interface_compilation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)

# Register test with CTest
gtest_discover_tests(test_interface_compilation
    PROPERTIES LABELS "unit"
)