cmake_minimum_required(VERSION 3.10)
project(Rua VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)

# ========== 平台检测 ==========
if(WIN32)
    add_compile_definitions(_WIN)
    set(PLATFORM_SOURCES
        arch/JITPlatform_windows.cpp
        arch/Console_windows.cpp
    )
elseif(UNIX)
    add_compile_definitions(_LINUX)
    set(PLATFORM_SOURCES
        arch/JITPlatform_linux.cpp
        arch/Console_linux.cpp
    )
endif()

# ========== 编译器标志 ==========
if(MSVC)
    add_compile_options(/utf-8 /W3 /O2)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -Wall -march=native -flto")
endif()

# ========== 优化器选项 ==========
option(ENABLE_OPTIMIZATION "Enable TAC IR and optimization passes" OFF)

# ========== 源文件 ==========
set(RUA_CORE_SOURCES
    src/main.cpp
    src/REPL.cpp
    src/UTF32支持.cpp
    src/词法分析器.cpp
    src/语法分析器.cpp
    src/语义分析.cpp
    src/字节码生成.cpp
    src/虚拟机.cpp
    src/JIT.cpp
)

# Optimization IR and passes
if(ENABLE_OPTIMIZATION)
    add_compile_definitions(OPTIMIZATION)
    list(APPEND RUA_CORE_SOURCES
        src/IR/中间指令.cpp
        src/IR/基本块.cpp
        src/IR/控制流图.cpp
        src/IR/支配树.cpp
        src/中间生成.cpp
        src/Optimizer/优化管理器.cpp
        src/Optimizer/常量传播.cpp
        src/Optimizer/死代码消除.cpp
        src/Optimizer/循环不变量外提.cpp
        src/Optimizer/循环展开.cpp
        src/Optimizer/函数内联.cpp
        src/Optimizer/强度削减.cpp
    )
endif()

add_executable(Rua
    ${RUA_CORE_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(Rua PRIVATE includes arch)

# ========== 调试宏 ==========
if(WIN32)
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
else()
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
endif()
