cmake_minimum_required(VERSION 3.16)
project(3BX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    src/lexer/token.cpp
)

set(PARSER_SOURCES
    src/parser/parser.cpp
)

set(AST_SOURCES
    src/ast/ast.cpp
)

set(SEMANTIC_SOURCES
    src/semantic/semantic.cpp
)

set(CODEGEN_SOURCES
    src/codegen/codegen.cpp
)

set(PATTERN_SOURCES
    src/pattern/pattern.cpp
    src/pattern/pattern_registry.cpp
    src/pattern/pattern_matcher.cpp
    src/pattern/patternTree.cpp
)

set(LSP_SOURCES
    src/lsp/lspServer.cpp
)

# Main executable
add_executable(3bx
    src/main.cpp
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${AST_SOURCES}
    ${SEMANTIC_SOURCES}
    ${CODEGEN_SOURCES}
    ${PATTERN_SOURCES}
    ${LSP_SOURCES}
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
