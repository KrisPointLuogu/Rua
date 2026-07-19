cmake_minimum_required(VERSION 3.10)
project(Rua VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)

if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

add_executable(Rua
    Rua/main.cpp
    Rua/REPL.cpp
    Rua/UTF32支持.cpp
    Rua/词法分析器.cpp
)

target_include_directories(Rua PRIVATE Rua)

if(WIN32)
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
else()
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
endif()
