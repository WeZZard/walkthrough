# ===========================================
# Agent Unit Tests
# ===========================================

# Agent mode state machine tests (no Frida dependency)

add_executable(test_agent_mode
    test_agent_mode.cpp
)
target_include_directories(test_agent_mode
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_mode
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_mode
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_mode
    RUNTIME DESTINATION bin
)

# Additional agent state machine tests
add_executable(test_agent_state_machine
    test_agent_state_machine.cpp
)
target_include_directories(test_agent_state_machine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_state_machine
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_state_machine
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_state_machine
    RUNTIME DESTINATION bin
)

# Agent coverage tests - exercises agent code paths for coverage
add_executable(test_agent_coverage
    test_agent_coverage.cpp
)
target_include_directories(test_agent_coverage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_coverage
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_coverage
    RUNTIME DESTINATION bin
)

# Agent mode tick unit test
add_executable(test_agent_mode_tick
    test_agent_mode_tick_before_captures.cpp
)
target_include_directories(test_agent_mode_tick
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_mode_tick
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_mode_tick
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_mode_tick
    RUNTIME DESTINATION bin
)
