# ===========================================
# Utils Unit Tests
# ===========================================

# Ring Buffer test
add_executable(test_ring_buffer
    test_ring_buffer.cpp
)
target_link_libraries(test_ring_buffer
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_ring_buffer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Ring Buffer attach test
add_executable(test_ring_buffer_attach
    test_ring_buffer_attach.cpp
)
target_link_libraries(test_ring_buffer_attach
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_ring_buffer_attach
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Shared Memory test
add_executable(test_shared_memory
    test_shared_memory.cpp
)
target_link_libraries(test_shared_memory
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_shared_memory
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Thread Registry test
add_executable(test_thread_registry
    test_thread_registry.cpp
)
target_include_directories(test_thread_registry
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/utils
        ${CMAKE_SOURCE_DIR}/src/utils  # For private headers
)
target_link_libraries(test_thread_registry
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
    tracer_utils
    Threads::Threads
)

# Offsets materialization tests (prototype for offsets-only SHM)
add_executable(test_offsets_materialization
    test_offsets_materialization.cpp
)
target_include_directories(test_offsets_materialization
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/utils
        ${CMAKE_SOURCE_DIR}/src/utils
)
target_link_libraries(test_offsets_materialization
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# ADA TLS + Registration test
add_executable(test_thread_registration_tls
    test_thread_registration_tls.cpp
)
target_include_directories(test_thread_registration_tls
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_thread_registration_tls
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# Test Discovery
gtest_discover_tests(test_ring_buffer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_ring_buffer_attach
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_shared_memory
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registry
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registration_tls
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_offsets_materialization
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# Control block IPC helpers tests
add_executable(test_control_block_ipc
    test_control_block_ipc.cpp
)
target_include_directories(test_control_block_ipc
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_control_block_ipc
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
gtest_discover_tests(test_control_block_ipc
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# Install targets
install(TARGETS
    test_ring_buffer
    test_ring_buffer_attach
    test_shared_memory
    test_thread_registry
    test_thread_registration_tls
    test_offsets_materialization
    test_control_block_ipc
    RUNTIME DESTINATION bin
)
