# ===========================================
# Controller Integration Tests
# ===========================================

# Check if Frida is available
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Controller lifecycle test
    add_executable(test_controller_full_lifecycle
        test_controller_full_lifecycle.cpp
    )
    target_include_directories(test_controller_full_lifecycle
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_controller_full_lifecycle
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Integration test
    add_executable(test_integration
        test_integration.cpp
    )
    target_include_directories(test_integration
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_integration
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Registry lifecycle tests (warm-up, stall, epoch roll)
    add_executable(test_registry_lifecycle
        test_registry_lifecycle.cpp
    )
    target_include_directories(test_registry_lifecycle
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_registry_lifecycle
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Test Discovery
    gtest_discover_tests(test_controller_full_lifecycle
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_integration
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_registry_lifecycle
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    
    # Install targets
    install(TARGETS
        test_controller_full_lifecycle
        test_integration
        test_registry_lifecycle
        RUNTIME DESTINATION bin
    )
endif()
