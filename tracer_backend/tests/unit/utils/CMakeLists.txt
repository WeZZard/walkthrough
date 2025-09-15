# ===========================================
# Utils Unit Tests
# ===========================================

# Enable testing hooks in utils library for unit tests
if (TARGET tracer_utils)
    target_compile_definitions(tracer_utils PUBLIC ADA_TESTING=1)
endif()

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

# SHM Directory tests (M1_E1_I8)
add_executable(test_shm_directory
    test_shm_directory.cpp
)
target_include_directories(test_shm_directory
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_shm_directory
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
    Threads::Threads
)

# Thread Registry fallback tests (exercise local-base path)
add_executable(test_thread_registry_fallback
    test_thread_registry_fallback.cpp
)
target_include_directories(test_thread_registry_fallback
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
)
target_link_libraries(test_thread_registry_fallback
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
gtest_discover_tests(test_shm_directory
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(test_thread_registry_fallback
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

# New tests: SPSC queue and RingPool swap protocol
add_executable(test_spsc_queue
    test_spsc_queue.cpp
)
target_link_libraries(test_spsc_queue
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_spsc_queue
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
gtest_discover_tests(test_spsc_queue
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

add_executable(test_ring_pool_swap
    test_ring_pool_swap.cpp
)
target_link_libraries(test_ring_pool_swap
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_ring_pool_swap
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
)
gtest_discover_tests(test_ring_pool_swap
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# Thread Pools tests (M1_E1_I9)
add_executable(test_thread_pools
    test_thread_pools.cpp
)
target_link_libraries(test_thread_pools
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)
target_include_directories(test_thread_pools
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils
)
gtest_discover_tests(test_thread_pools
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
    test_shm_directory
    test_thread_registry_fallback
    test_spsc_queue
    test_ring_pool_swap
    test_thread_pools
    RUNTIME DESTINATION bin
)
