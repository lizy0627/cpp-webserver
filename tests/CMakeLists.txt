add_executable(WebServerCoreTests
    test_core.cpp
)

webserver_apply_common_options(WebServerCoreTests)

set(WEBSERVER_TEST_RUNTIME_DIR ${CMAKE_BINARY_DIR}/tests/bin)

set_target_properties(WebServerCoreTests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${WEBSERVER_TEST_RUNTIME_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${WEBSERVER_TEST_RUNTIME_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${WEBSERVER_TEST_RUNTIME_DIR}
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${WEBSERVER_TEST_RUNTIME_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${WEBSERVER_TEST_RUNTIME_DIR}
)

target_link_libraries(WebServerCoreTests
    PRIVATE
        WebServerCore
)

target_compile_definitions(WebServerCoreTests
    PRIVATE
        WEBSERVER_TEST_WWW_DIR="${PROJECT_SOURCE_DIR}/www"
)

add_test(
    NAME WebServerCoreTests
    COMMAND ${WEBSERVER_TEST_RUNTIME_DIR}/WebServerCoreTests${CMAKE_EXECUTABLE_SUFFIX}
)
