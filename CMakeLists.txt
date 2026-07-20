cmake_minimum_required(VERSION 3.10)
project(Rua VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)

if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -Wall")

add_executable(Rua
    src/main.cpp
    src/REPL.cpp
    src/UTF32支持.cpp
    src/词法分析器.cpp
    src/语法分析器.cpp
    src/语义分析.cpp
    src/字节码生成.cpp
    src/虚拟机.cpp
)

target_include_directories(Rua PRIVATE includes)

if(WIN32)
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
else()
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
endif()
