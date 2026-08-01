cmake_minimum_required(VERSION 3.10)
project(Rua VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)

# ========== Platform Detection ==========
if(WIN32)
    add_compile_definitions(_WIN)
    set(PLATFORM_SOURCES
        arch/JITPlatform_windows.cpp
        arch/Console_windows.cpp
        arch/FileSystem_windows.cpp
        arch/CommandLine_windows.cpp
    )
elseif(UNIX)
    add_compile_definitions(_LINUX)
    set(PLATFORM_SOURCES
        arch/JITPlatform_linux.cpp
        arch/Console_linux.cpp
        arch/FileSystem_linux.cpp
        arch/CommandLine_linux.cpp
    )
endif()

# ========== Compiler Flags ==========
if(MSVC)
    add_compile_options(/utf-8 /W3 /O2)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -Wall -march=native -flto -Wextra")
endif()

# ========== Optimizer Options ==========
option(ENABLE_OPTIMIZATION "Enable TAC IR and optimization passes" OFF)

# ========== Source Files ==========
set(RUA_CORE_SOURCES
    src/main.cpp
    src/REPL.cpp
    src/UTF32Support.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/SemanticAnalysis.cpp
    src/BytecodeGenerator.cpp
    src/VM.cpp
    src/JIT.cpp
)

# Optimization IR and passes
if(ENABLE_OPTIMIZATION)
    add_compile_definitions(OPTIMIZATION)
    list(APPEND RUA_CORE_SOURCES
        src/IR/TACInstructions.cpp
        src/IR/BasicBlock.cpp
        src/IR/CFG.cpp
        src/IR/DominatorTree.cpp
        src/TACGenerator.cpp
        src/Optimizer/PassManager.cpp
        src/Optimizer/ConstantPropagation.cpp
        src/Optimizer/DCE.cpp
        src/Optimizer/LICM.cpp
        src/Optimizer/LoopUnrolling.cpp
        src/Optimizer/FunctionInlining.cpp
        src/Optimizer/StrengthReduction.cpp
    )
endif()

add_executable(Rua
    ${RUA_CORE_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(Rua PRIVATE includes arch)

# ========== Platform Libraries ==========
if(WIN32)
    # CommandLineToArgvW 位于 shell32
    target_link_libraries(Rua PRIVATE shell32)
endif()

# ========== Debug Macros ==========
if(WIN32)
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
else()
    target_compile_definitions(Rua PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
endif()
