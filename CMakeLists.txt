cmake_minimum_required(VERSION 3.10)

project(CppWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEBSERVER_ENABLE_ASAN "Enable AddressSanitizer for supported compilers" OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "CppWebServer uses Linux epoll and POSIX sockets; build and run it on Linux.")
endif()

find_package(Threads REQUIRED)

function(webserver_apply_common_options target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                $<$<CONFIG:Debug>:-g>
        )
    endif()

    if(WEBSERVER_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name}
            PRIVATE
                -fsanitize=address
                -fno-omit-frame-pointer
        )
        target_link_libraries(${target_name}
            PRIVATE
                -fsanitize=address
        )
    endif()
endfunction()

set(WEBSERVER_CORE_SOURCES
    src/CommandLineOptions.cpp
    src/Config.cpp
    src/HttpParser.cpp
    src/HttpResponse.cpp
    src/Logger.cpp
    src/StaticFileHandler.cpp
    src/ThreadPool.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND WEBSERVER_CORE_SOURCES src/Server.cpp)
else()
    list(APPEND WEBSERVER_CORE_SOURCES src/ServerUnsupported.cpp)
endif()

add_library(WebServerCore
    ${WEBSERVER_CORE_SOURCES}
)

target_include_directories(WebServerCore
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(WebServerCore
    PUBLIC
        Threads::Threads
)

webserver_apply_common_options(WebServerCore)

add_executable(WebServer
    src/main.cpp
)

target_link_libraries(WebServer
    PRIVATE
        WebServerCore
)

webserver_apply_common_options(WebServer)

enable_testing()

if(CMAKE_CONFIGURATION_TYPES)
    set_property(
        DIRECTORY
        PROPERTY TEST_INCLUDE_FILE
            ${PROJECT_SOURCE_DIR}/cmake/CTestDefaultConfig.cmake
    )
endif()

add_subdirectory(tests)
