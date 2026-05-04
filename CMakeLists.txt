cmake_minimum_required(VERSION 3.10)

# 定义项目名称和使用的编程语言。
project(CppWebServer LANGUAGES CXX)

# 使用 C++17 标准，并要求编译器必须支持该标准。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 创建可执行程序，并明确列出项目源文件。
add_executable(WebServer
    src/main.cpp
    src/Server.cpp
)

# 添加公共头文件搜索目录，让源文件可以包含 include/Server.h。
target_include_directories(WebServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
