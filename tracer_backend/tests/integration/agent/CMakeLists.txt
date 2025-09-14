# ===========================================
# Agent Integration Tests
# ===========================================

# Check if Frida is available
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Agent loader test
    add_executable(test_agent_loader
        test_agent_loader.cpp
    )
    target_include_directories(test_agent_loader
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_agent_loader
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Baseline hooks test
    add_executable(test_baseline_hooks
        test_baseline_hooks.cpp
    )
    target_include_directories(test_baseline_hooks
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_baseline_hooks
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )

    # Registry mode handling tests
    add_executable(test_agent_registry_modes
        test_agent_registry_modes.cpp
    )
    target_include_directories(test_agent_registry_modes
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_agent_registry_modes
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )
    
    # Test Discovery
    gtest_discover_tests(test_agent_loader
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_baseline_hooks
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_agent_registry_modes
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    
    # Install targets
    install(TARGETS
        test_agent_loader
        test_baseline_hooks
        test_agent_registry_modes
        RUNTIME DESTINATION bin
    )
endif()
