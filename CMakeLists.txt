cmake_minimum_required(VERSION 3.10)

# 定义项目名称和使用的编程语言。
project(CppWebServer LANGUAGES CXX)

# 使用 C++17 标准，并要求编译器必须支持该标准。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 使用 UTF-8 读取源码，保证中文注释不会乱码。
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 创建可执行程序，并明确列出项目源文件。
add_executable(WebServer
    src/main.cpp
    src/Server.cpp
)

# 添加公共头文件搜索目录。
target_include_directories(WebServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Windows 下链接 Winsock 库，提供 socket、bind、listen、accept 等 API。
if(WIN32)
    target_link_libraries(WebServer PRIVATE ws2_32)
endif()
