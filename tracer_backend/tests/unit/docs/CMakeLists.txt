add_executable(test_docs_unit
    test_docs_unit.cpp
)

target_link_libraries(test_docs_unit
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_docs
        Threads::Threads
)

target_include_directories(test_docs_unit
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(test_docs_unit
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
