cmake_minimum_required(VERSION 3.16)
project(3BX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable debug symbols
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files by component
set(LEXER_SOURCES
    src/lexer/lexer.cpp
)

set(LSP_SOURCES
    src/lsp/lspServer.cpp
    src/lsp/lspServerHandlers.cpp
    src/lsp/lspServerFeatures.cpp
)

set(DAP_SOURCES
    src/dap/dapServer.cpp
)

set(COMPILER_SOURCES
    src/compiler/importResolver.cpp
    src/compiler/sectionAnalyzer.cpp
    src/compiler/patternResolver.cpp
    src/compiler/patternResolverExtract.cpp
    src/compiler/patternResolverTree.cpp
    src/compiler/resolvedPattern.cpp
    src/compiler/patternMatch.cpp
    src/compiler/patternTree.cpp
    src/compiler/typeInference.cpp
    src/compiler/codeGenerator.cpp
    src/compiler/codeGeneratorTypes.cpp
    src/compiler/codeGeneratorPatterns.cpp
    src/compiler/codeGeneratorIntrinsics.cpp
    src/compiler/codeGeneratorExpressions.cpp
    src/compiler/optimizer.cpp
)

# Main executable
add_executable(3bx
    src/main.cpp
    ${LEXER_SOURCES}
    ${LSP_SOURCES}
    ${DAP_SOURCES}
    ${COMPILER_SOURCES}
)

# Link LLVM libraries
llvm_map_components_to_libnames(llvm_libs
    core
    support
    irreader
    passes
    native
    orcjit
    mcjit
)

target_link_libraries(3bx ${llvm_libs})

# Enable testing
enable_testing()
add_subdirectory(tests)
