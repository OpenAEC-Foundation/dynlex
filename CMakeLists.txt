#
# Specify minimum CMake version and project name
#
cmake_minimum_required (VERSION 3.21)
set(DYNLEX_VERSION_FILE "${CMAKE_SOURCE_DIR}/metadata/VERSION")
if(NOT EXISTS "${DYNLEX_VERSION_FILE}")
    message(FATAL_ERROR "Missing VERSION file at ${DYNLEX_VERSION_FILE}")
endif()
file(READ "${DYNLEX_VERSION_FILE}" DYNLEX_VERSION_RAW)
string(STRIP "${DYNLEX_VERSION_RAW}" DYNLEX_VERSION)
if(NOT DYNLEX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid version in VERSION file: '${DYNLEX_VERSION}'")
endif()
project (dynlex VERSION ${DYNLEX_VERSION})

include(GNUInstallDirs)

# Homebrew Clang selects the macOS SDK through its driver configuration, but
# CMake does not always include that configured sysroot in its detected implicit
# header directories. LLVM's imported targets can then re-export SDK headers
# used by system libraries as explicit -isystem paths.
# Mark the active SDK headers as implicit so CMake omits them from every target's
# command line and preserves libc++'s wrapper-header ordering.
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE DYNLEX_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DYNLEX_MACOS_SDK_RESULT
        ERROR_QUIET
    )
    set(DYNLEX_MACOS_SDK_SYSTEM_INCLUDE "${DYNLEX_MACOS_SDK}/usr/include")
    if(NOT DYNLEX_MACOS_SDK_RESULT EQUAL 0 OR NOT IS_DIRECTORY "${DYNLEX_MACOS_SDK_SYSTEM_INCLUDE}")
        message(FATAL_ERROR "Unable to locate the active macOS SDK headers through xcrun")
    endif()
    list(APPEND CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "${DYNLEX_MACOS_SDK_SYSTEM_INCLUDE}")
    list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "${DYNLEX_MACOS_SDK_SYSTEM_INCLUDE}")
    list(REMOVE_DUPLICATES CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)
    list(REMOVE_DUPLICATES CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
endif()

#
# Enable ccache if available
#
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

#
# Generate compile_commands.json for clang-tidy
#
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# nlohmann_json is header-only, so Emscripten must use the host package.  Keep
# LLVM and every compiled dependency restricted to the target sysroot.
if(EMSCRIPTEN)
    find_package(nlohmann_json CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)
else()
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

if(TARGET nlohmann_json::nlohmann_json)
    set(DYNLEX_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
    get_target_property(DYNLEX_NLOHMANN_ALIASED_TARGET nlohmann_json::nlohmann_json ALIASED_TARGET)
    if(DYNLEX_NLOHMANN_ALIASED_TARGET)
        set(DYNLEX_NLOHMANN_TARGET "${DYNLEX_NLOHMANN_ALIASED_TARGET}")
    endif()

    get_target_property(DYNLEX_NLOHMANN_INCLUDE_DIRS ${DYNLEX_NLOHMANN_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(DYNLEX_NLOHMANN_INCLUDE_DIRS)
        if(EMSCRIPTEN)
            set(DYNLEX_NLOHMANN_STAGING_DIR "${CMAKE_BINARY_DIR}/dependencies/include")
            set(DYNLEX_NLOHMANN_SOURCE_DIR "")
            foreach(DYNLEX_NLOHMANN_INCLUDE_DIR IN LISTS DYNLEX_NLOHMANN_INCLUDE_DIRS)
                if(EXISTS "${DYNLEX_NLOHMANN_INCLUDE_DIR}/nlohmann/json.hpp")
                    set(DYNLEX_NLOHMANN_SOURCE_DIR "${DYNLEX_NLOHMANN_INCLUDE_DIR}/nlohmann")
                    break()
                endif()
            endforeach()

            if(NOT DYNLEX_NLOHMANN_SOURCE_DIR)
                message(FATAL_ERROR "nlohmann_json does not provide its required nlohmann/json.hpp header")
            endif()

            file(REMOVE_RECURSE "${DYNLEX_NLOHMANN_STAGING_DIR}/nlohmann")
            file(COPY "${DYNLEX_NLOHMANN_SOURCE_DIR}" DESTINATION "${DYNLEX_NLOHMANN_STAGING_DIR}")
            set(DYNLEX_NLOHMANN_INCLUDE_DIRS "${DYNLEX_NLOHMANN_STAGING_DIR}")
            set_target_properties(${DYNLEX_NLOHMANN_TARGET} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${DYNLEX_NLOHMANN_INCLUDE_DIRS}"
            )
        endif()

        set_target_properties(${DYNLEX_NLOHMANN_TARGET} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${DYNLEX_NLOHMANN_INCLUDE_DIRS}"
        )
    endif()
endif()

set(DYNLEX_LLVM_METADATA_FILE "${CMAKE_SOURCE_DIR}/metadata/LLVM_TOOLCHAIN")
if(NOT EXISTS "${DYNLEX_LLVM_METADATA_FILE}")
    message(FATAL_ERROR "Missing LLVM toolchain metadata at ${DYNLEX_LLVM_METADATA_FILE}")
endif()
file(STRINGS "${DYNLEX_LLVM_METADATA_FILE}" DYNLEX_LLVM_METADATA)
foreach(DYNLEX_LLVM_METADATA_LINE IN LISTS DYNLEX_LLVM_METADATA)
    if(NOT DYNLEX_LLVM_METADATA_LINE MATCHES "^([a-z]+) (.+)$")
        message(FATAL_ERROR "Invalid LLVM toolchain metadata: ${DYNLEX_LLVM_METADATA_LINE}")
    endif()
    string(TOUPPER "${CMAKE_MATCH_1}" DYNLEX_LLVM_METADATA_KEY)
    set("DYNLEX_LLVM_${DYNLEX_LLVM_METADATA_KEY}" "${CMAKE_MATCH_2}")
endforeach()
foreach(DYNLEX_LLVM_REQUIRED_FIELD IN ITEMS REPOSITORY REVISION MAJOR BOOTSTRAP SCHEMA)
    if(NOT DEFINED "DYNLEX_LLVM_${DYNLEX_LLVM_REQUIRED_FIELD}")
        message(FATAL_ERROR "LLVM toolchain metadata is missing ${DYNLEX_LLVM_REQUIRED_FIELD}")
    endif()
endforeach()
set(DYNLEX_REQUIRED_LLVM_VERSION "${DYNLEX_LLVM_MAJOR}")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL "${DYNLEX_REQUIRED_LLVM_VERSION}.0.0git")
    message(FATAL_ERROR "DynLex requires the pinned LLVM ${DYNLEX_REQUIRED_LLVM_VERSION} development toolchain")
endif()

get_filename_component(DYNLEX_LLVM_INSTALL_LIB_DIR "${LLVM_DIR}/../.." ABSOLUTE)
get_filename_component(DYNLEX_LLVM_INSTALL_DIR "${DYNLEX_LLVM_INSTALL_LIB_DIR}/.." ABSOLUTE)
set(DYNLEX_LLVM_TOOLCHAIN_MARKER "${DYNLEX_LLVM_INSTALL_DIR}/.dynlex-llvm-toolchain")
if(NOT EXISTS "${DYNLEX_LLVM_TOOLCHAIN_MARKER}")
    message(FATAL_ERROR "LLVM toolchain is missing its DynLex revision marker; run scripts/build_llvm.sh")
endif()
file(READ "${DYNLEX_LLVM_TOOLCHAIN_MARKER}" DYNLEX_LLVM_TOOLCHAIN_ID)
string(STRIP "${DYNLEX_LLVM_TOOLCHAIN_ID}" DYNLEX_LLVM_TOOLCHAIN_ID)
if(EMSCRIPTEN)
    set(DYNLEX_LLVM_REQUIRED_PROFILE web)
else()
    set(DYNLEX_LLVM_REQUIRED_PROFILE native)
endif()
set(DYNLEX_EXPECTED_LLVM_TOOLCHAIN_ID
    "${DYNLEX_LLVM_REVISION}:${DYNLEX_LLVM_SCHEMA}:${DYNLEX_LLVM_REQUIRED_PROFILE}"
)
if(NOT DYNLEX_LLVM_TOOLCHAIN_ID STREQUAL DYNLEX_EXPECTED_LLVM_TOOLCHAIN_ID)
    message(FATAL_ERROR "LLVM toolchain does not match the pinned DynLex ${DYNLEX_LLVM_REQUIRED_PROFILE} profile")
endif()
set(DYNLEX_LLVM_VCS_HEADER "${DYNLEX_LLVM_INSTALL_DIR}/include/llvm/Support/VCSRevision.h")
if(NOT EXISTS "${DYNLEX_LLVM_VCS_HEADER}")
    message(FATAL_ERROR "LLVM toolchain is missing its embedded source revision")
endif()
file(READ "${DYNLEX_LLVM_VCS_HEADER}" DYNLEX_LLVM_VCS_ID)
set(DYNLEX_EXPECTED_LLVM_VCS_ID
    "#define LLVM_REVISION R\"(${DYNLEX_LLVM_REVISION})\"\n#define LLVM_REPOSITORY R\"(${DYNLEX_LLVM_REPOSITORY})\"\n"
)
if(NOT DYNLEX_LLVM_VCS_ID STREQUAL DYNLEX_EXPECTED_LLVM_VCS_ID)
    message(FATAL_ERROR "LLVM toolchain was not built from the pinned DynLex fork")
endif()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

#
# CMake setup
#
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set (CMAKE_VERBOSE_MAKEFILE 0) # 1 should be used for debugging
set (CMAKE_SUPPRESS_REGENERATION TRUE) # Suppresses ZERO_CHECK
set(CMAKE_CXX_STANDARD 23)
option(DYNLEX_WEB "Build browser compiler target (Emscripten)" OFF)

# The normal compiler is optimized but keeps complete debug information and
# assertions. This is intentionally distinct from RelWithDebInfo, whose
# platform defaults define NDEBUG.
if(MSVC)
    set(CMAKE_C_FLAGS_OPTIMIZED "/O2 /Zi" CACHE STRING "Flags for optimized DynLex C builds" FORCE)
    set(CMAKE_CXX_FLAGS_OPTIMIZED "/O2 /Zi" CACHE STRING "Flags for optimized DynLex C++ builds" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_OPTIMIZED "/DEBUG" CACHE STRING "Linker flags for optimized DynLex builds" FORCE)
else()
    set(CMAKE_C_FLAGS_OPTIMIZED "-O2 -g" CACHE STRING "Flags for optimized DynLex C builds" FORCE)
    set(CMAKE_CXX_FLAGS_OPTIMIZED "-O2 -g" CACHE STRING "Flags for optimized DynLex C++ builds" FORCE)
endif()
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND CMAKE_CONFIGURATION_TYPES Optimized)
    list(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Available build configurations" FORCE)
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
  if(NOT WIN32)
    set(GLAD_LIBRARIES dl)
  endif()
endif()


#
# GLFW options
#
option (GLFW_INSTALL OFF)
option (GLFW_BUILD_DOCS OFF)
option (GLFW_BUILD_EXAMPLES OFF)
option (GLFW_BUILD_TESTS OFF)
#add_subdirectory (gloom/vendor/glfw)

MACRO(HEADER_DIRECTORIES return_list)
    FILE(GLOB_RECURSE new_list src/cpp/*.h)
    SET(dir_list "")
    FOREACH(file_path ${new_list})
        GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
        SET(dir_list ${dir_list} ${dir_path})
    ENDFOREACH()
    LIST(REMOVE_DUPLICATES dir_list)
    SET(${return_list} ${dir_list})
ENDMACRO()

HEADER_DIRECTORIES(header_dir_list)

#
# Set include paths
#
include_directories (${header_dir_list})
include_directories (src/cpp)


#
# Add files
#
file (GLOB_RECURSE PROJECT_HEADERS src/cpp/*.hpp
                                   src/cpp/*.h)
file (GLOB_RECURSE PROJECT_SOURCES src/cpp/*.cpp
                                   src/cpp/*.cxx
                                   src/cpp/*.cc
                                   src/cpp/*.c)
file (GLOB_RECURSE DYNLEX_LIBRARY_SOURCES CONFIGURE_DEPENDS
                                      "${CMAKE_SOURCE_DIR}/lib/*.dl")
file (GLOB         PROJECT_CONFIGS CMakeLists.txt
                                   README.md
                                  .gitignore)

#
# Organizing files
#
source_group ("headers" FILES ${PROJECT_HEADERS})
source_group ("sources" FILES ${PROJECT_SOURCES})

#
# Set executable and target link libraries
#
add_definitions (-DGLFW_INCvLUDE_NONE
                 -DPROJECT_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\")

#target_include_directories(${PROJECT_NAME}
#PRIVATE  ${Stb_INCLUDE_DIR}
#)
# The pinned DynLex LLVM distribution contains static component libraries.
function(dynlex_link_llvm target)
    message(STATUS "Linking ${target} against LLVM component libraries")
    llvm_config(${target} ${ARGN})
endfunction()

set(DYNLEX_LLVM_COMMON_COMPONENTS
    core
    support
    irreader
    passes
    WebAssemblyCodeGen
    WebAssemblyDesc
    WebAssemblyInfo
    WebAssemblyUtils
)

# The pinned LLVM 23 fork includes the SPIR-V and WebAssembly backends.
# Native builds additionally need the host backend.
set(DYNLEX_LLVM_NATIVE_COMPONENTS
    ${DYNLEX_LLVM_COMMON_COMPONENTS}
    native
    SPIRV
)

set(DYNLEX_NATIVE_SOURCES ${PROJECT_SOURCES})

# Web-only host bridge should only be part of the web module.
list(FILTER DYNLEX_NATIVE_SOURCES EXCLUDE REGEX "src/cpp/web/.*\\.cpp$")

if(EMSCRIPTEN AND NOT DYNLEX_WEB)
    message(FATAL_ERROR "Emscripten builds must enable DYNLEX_WEB=ON.")
endif()

if(NOT EMSCRIPTEN)
    set(DYNLEX_RUNTIME_SOURCES
        src/runtime/filesystemRuntime.c
        src/runtime/hostRuntime.c
        src/runtime/pathRuntime.c
        src/runtime/pathUriRuntime.c
        src/runtime/processRuntime.c
        src/runtime/runtimeError.c
        src/runtime/runtimeText.c
    )
    if(WIN32)
        list(APPEND DYNLEX_RUNTIME_SOURCES
            src/runtime/filesystemRuntimeWindows.c
            src/runtime/filesystemTransactionWindows.c
            src/runtime/hostRuntimeWindows.c
            src/runtime/hostRuntimeWindowsPath.c
            src/runtime/processRuntimeWindows.c
            src/runtime/processRuntimeWindowsQuoting.c
        )
    else()
        list(APPEND DYNLEX_RUNTIME_SOURCES
            src/runtime/filesystemRuntimePosix.c
            src/runtime/filesystemTransactionPosix.c
            src/runtime/hostRuntimePosix.c
            src/runtime/processRuntimePosix.c
        )
    endif()
    add_library(dynlex_runtime STATIC ${DYNLEX_RUNTIME_SOURCES})
    set_target_properties(dynlex_runtime PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(dynlex_runtime PRIVATE /W4 /WX)
    else()
        target_compile_options(dynlex_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_executable (${PROJECT_NAME} ${DYNLEX_NATIVE_SOURCES} ${PROJECT_HEADERS}
                                    ${PROJECT_CONFIGS})
    add_dependencies(${PROJECT_NAME} dynlex_runtime)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DYNLEX_RUNTIME_LIBRARY_BUILD_PATH="$<TARGET_FILE:dynlex_runtime>"
        DYNLEX_RUNTIME_LIBRARY_FILENAME="$<TARGET_FILE_NAME:dynlex_runtime>"
        DYNLEX_RUNTIME_LIBRARY_INSTALL_DIR="${CMAKE_INSTALL_LIBDIR}/dynlex"
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
    dynlex_link_llvm(${PROJECT_NAME} ${DYNLEX_LLVM_NATIVE_COMPONENTS})
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 shell32 ole32)
    endif()
    set_target_properties (${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE DYNLEX_VERSION="${PROJECT_VERSION}")

    add_executable(
        dynlex_class_layout_test EXCLUDE_FROM_ALL
        tests/cpp/classLayoutTest.cpp
        src/cpp/compiler/classLayout.cpp
    )
    target_include_directories(dynlex_class_layout_test PRIVATE src/cpp)
    dynlex_link_llvm(dynlex_class_layout_test core support)
    set_target_properties(dynlex_class_layout_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # MinGW stores full DWARF in every COFF object for a normal Debug build.
    # Keeping that data in the objects makes LLD spend an unbounded amount of
    # time producing the final PE image.  Split DWARF preserves the complete
    # debugger information in adjacent .dwo files while leaving only the
    # address-bearing skeleton records for the linker.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE
            "$<$<OR:$<CONFIG:Debug>,$<CONFIG:Optimized>>:-gsplit-dwarf>"
        )
    endif()

    add_executable(fbx_to_obj tools/fbx_to_obj.cpp third_party/ufbx/ufbx.c)
    target_include_directories(
        fbx_to_obj SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/ufbx ${CMAKE_SOURCE_DIR}/third_party/stb
    )
    set_target_properties(fbx_to_obj PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

if(DYNLEX_WEB)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "DYNLEX_WEB requires the Emscripten toolchain. Configure with emcmake.")
    endif()

    include(FetchContent)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        spirv_cross
        URL https://github.com/KhronosGroup/SPIRV-Cross/archive/a0fba56c34a6700f1724bf9b751da5b488a3775c.tar.gz
        URL_HASH SHA256=1e79592b0a498ffb854f7d71e0d5811fcedb87a25201644bbf5dbba438e01264
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(spirv_cross)

    set(DYNLEX_WEB_SOURCES ${PROJECT_SOURCES})
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/main\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/dap/.*\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/lsp/tcpTransport\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/lsp/stdioTransport\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/compiler/codegen/native\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/pexlit/rendering/.*\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/pexlit/safeInclude/implementations\\.cpp$")

    add_executable(dynlex_web ${DYNLEX_WEB_SOURCES} ${PROJECT_HEADERS} ${PROJECT_CONFIGS})
    set_target_properties(dynlex_web PROPERTIES
        OUTPUT_NAME "dynlex_web"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LINK_DEPENDS "${DYNLEX_LIBRARY_SOURCES}"
    )
    target_compile_definitions(dynlex_web PRIVATE DYNLEX_WEB=1 DYNLEX_VERSION="${PROJECT_VERSION}")
    target_compile_options(dynlex_web PRIVATE "-fexceptions")
    target_link_libraries(dynlex_web PRIVATE nlohmann_json::nlohmann_json spirv-cross-core spirv-cross-glsl)
    dynlex_link_llvm(dynlex_web ${DYNLEX_LLVM_COMMON_COMPONENTS} SPIRV)

    set(DYNLEX_WEB_EXPORTED_FUNCTIONS
        "['_dynlex_web_init','_dynlex_web_set_main_source','_dynlex_web_compile_and_emit_wasm','_dynlex_web_compile_and_emit_shader_glsl','_dynlex_web_get_diagnostics_json','_dynlex_web_get_output_wasm_ptr','_dynlex_web_get_output_wasm_len','_dynlex_web_get_output_wasm_base64','_dynlex_web_get_output_shader_glsl','_dynlex_web_get_shader_uniforms_json','_dynlex_web_get_compiler_log_json','_dynlex_web_lsp_exchange_json']"
    )
    set(DYNLEX_WEB_EXPORTED_RUNTIME_METHODS "['ccall','cwrap','UTF8ToString']")
    file(GLOB_RECURSE DYNLEX_WEB_EMBEDDED_LIBRARY_FILES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${CMAKE_SOURCE_DIR}/lib/*"
    )
    set_property(TARGET dynlex_web APPEND PROPERTY LINK_DEPENDS ${DYNLEX_WEB_EMBEDDED_LIBRARY_FILES})
    target_link_options(dynlex_web PRIVATE
        "-fexceptions"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sSTACK_SIZE=8388608"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web,worker,node"
        "-sNO_EXIT_RUNTIME=1"
        "-sFILESYSTEM=1"
        "-sEXPORTED_FUNCTIONS=${DYNLEX_WEB_EXPORTED_FUNCTIONS}"
        "-sEXPORTED_RUNTIME_METHODS=${DYNLEX_WEB_EXPORTED_RUNTIME_METHODS}"
        "--embed-file" "${CMAKE_SOURCE_DIR}/lib@/lib"
    )
endif()

#
# Install targets
#
if(TARGET ${PROJECT_NAME})
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TARGET dynlex_runtime)
    install(TARGETS dynlex_runtime ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/dynlex)
endif()
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/dynlex/lib FILES_MATCHING PATTERN "*.dl")
if(WIN32 AND TARGET ${PROJECT_NAME})
    # Bundle every DLL reported by the final link graph so the installation is self-contained.
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

#
# CPack packaging
#
if(WIN32)
    set(CPACK_GENERATOR "WIX;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild;TGZ")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
endif()
set(CPACK_PACKAGE_NAME "dynlex")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DynLex — a natural-language-like programming language")
set(CPACK_PACKAGE_CONTACT "DynLex maintainers <maintainers@dynlex.org>")
set(CPACK_PACKAGE_VENDOR "DynLex")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://dynlex.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
include(CPack)
