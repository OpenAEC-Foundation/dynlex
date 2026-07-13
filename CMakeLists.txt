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
# header directories. LLVM's imported targets then re-export SDK headers used by
# system libraries (FFI, LibEdit, zlib, and others) as explicit -isystem paths.
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

set(DYNLEX_MIN_LLVM_VERSION 20)
set(DYNLEX_LLVM_VERSION "" CACHE STRING "Preferred LLVM major version for DynLex")

if(NOT LLVM_DIR)
    if(DYNLEX_LLVM_VERSION)
        find_program(DYNLEX_LLVM_CONFIG NAMES llvm-config-${DYNLEX_LLVM_VERSION} llvm-config)
    else()
        find_program(DYNLEX_LLVM_CONFIG NAMES llvm-config)
    endif()

    if(DYNLEX_LLVM_CONFIG)
        execute_process(
            COMMAND ${DYNLEX_LLVM_CONFIG} --version
            OUTPUT_VARIABLE DYNLEX_LLVM_CONFIG_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        string(REGEX MATCH "^[0-9]+" DYNLEX_FOUND_LLVM_MAJOR "${DYNLEX_LLVM_CONFIG_VERSION}")
        if(DYNLEX_LLVM_VERSION AND NOT DYNLEX_FOUND_LLVM_MAJOR STREQUAL DYNLEX_LLVM_VERSION)
            message(FATAL_ERROR
                "Requested LLVM ${DYNLEX_LLVM_VERSION}, but ${DYNLEX_LLVM_CONFIG} resolves to LLVM ${DYNLEX_FOUND_LLVM_MAJOR}"
            )
        endif()

        execute_process(
            COMMAND ${DYNLEX_LLVM_CONFIG} --cmakedir
            OUTPUT_VARIABLE DYNLEX_LLVM_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(EXISTS "${DYNLEX_LLVM_CMAKE_DIR}/LLVMConfig.cmake")
            set(LLVM_DIR "${DYNLEX_LLVM_CMAKE_DIR}" CACHE PATH "LLVM CMake directory" FORCE)
        endif()
    endif()
endif()

if(WIN32 AND NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)

    if(NOT TARGET ZLIB::ZLIB)
        set(DYNLEX_ZLIB_HINTS "")
        if(LLVM_DIR)
            get_filename_component(DYNLEX_LLVM_LIB_DIR "${LLVM_DIR}/../.." ABSOLUTE)
            get_filename_component(DYNLEX_LLVM_ROOT_DIR "${DYNLEX_LLVM_LIB_DIR}/.." ABSOLUTE)
            list(APPEND DYNLEX_ZLIB_HINTS "${DYNLEX_LLVM_LIB_DIR}" "${DYNLEX_LLVM_ROOT_DIR}/lib")
        endif()

        find_library(DYNLEX_ZLIB_LIBRARY
            NAMES zlibstatic zlib libz z
            HINTS ${DYNLEX_ZLIB_HINTS}
        )
        find_path(DYNLEX_ZLIB_INCLUDE_DIR
            NAMES zlib.h
            HINTS ${DYNLEX_ZLIB_HINTS} "${DYNLEX_LLVM_ROOT_DIR}/include"
        )

        if(DYNLEX_ZLIB_LIBRARY)
            add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
            set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION "${DYNLEX_ZLIB_LIBRARY}")
            if(DYNLEX_ZLIB_INCLUDE_DIR)
                set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${DYNLEX_ZLIB_INCLUDE_DIR}")
            endif()
            message(STATUS "Using fallback ZLIB::ZLIB target: ${DYNLEX_ZLIB_LIBRARY}")
        endif()
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(LLVM_PACKAGE_VERSION VERSION_LESS "${DYNLEX_MIN_LLVM_VERSION}.0.0")
    message(FATAL_ERROR "DynLex requires LLVM ${DYNLEX_MIN_LLVM_VERSION} or newer")
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
# LLVM packages declare whether their components should be consumed through
# the monolithic LLVM shared library or as individual static archives.  Use
# llvm_config so that declaration is respected and any component omitted from
# a partial shared library is still linked explicitly.
function(dynlex_link_llvm target)
    if(LLVM_LINK_LLVM_DYLIB)
        if(NOT TARGET LLVM)
            message(FATAL_ERROR
                "LLVM_LINK_LLVM_DYLIB is enabled, but the LLVM package does not provide its shared LLVM target"
            )
        endif()
        message(STATUS "Linking ${target} against the shared LLVM library")
        llvm_config(${target} USE_SHARED ${ARGN})
    else()
        message(STATUS "Linking ${target} against LLVM component libraries")
        llvm_config(${target} ${ARGN})
    endif()
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

# LLVM 20+ includes the SPIR-V and WebAssembly backends as official targets.
# Native builds additionally need the host and SPIR-V backends.
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
    add_executable (${PROJECT_NAME} ${DYNLEX_NATIVE_SOURCES} ${PROJECT_HEADERS}
                                    ${PROJECT_CONFIGS})

    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
    dynlex_link_llvm(${PROJECT_NAME} ${DYNLEX_LLVM_NATIVE_COMPONENTS})
    set_target_properties (${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE DYNLEX_VERSION="${PROJECT_VERSION}")

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

    set(DYNLEX_WEB_SOURCES ${PROJECT_SOURCES})
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/main\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/dap/.*\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/lsp/tcpTransport\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/lsp/stdioTransport\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/compiler/codegen/native\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/compiler/codegen/spirv\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/pexlit/rendering/.*\\.cpp$")
    list(FILTER DYNLEX_WEB_SOURCES EXCLUDE REGEX "src/cpp/pexlit/safeInclude/implementations\\.cpp$")

    add_executable(dynlex_web ${DYNLEX_WEB_SOURCES} ${PROJECT_HEADERS} ${PROJECT_CONFIGS})
    set_target_properties(dynlex_web PROPERTIES
        OUTPUT_NAME "dynlex_web"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    target_compile_definitions(dynlex_web PRIVATE DYNLEX_WEB=1 DYNLEX_VERSION="${PROJECT_VERSION}")
    target_link_libraries(dynlex_web PRIVATE nlohmann_json::nlohmann_json)
    dynlex_link_llvm(dynlex_web ${DYNLEX_LLVM_COMMON_COMPONENTS})

    set(DYNLEX_WEB_EXPORTED_FUNCTIONS
        "['_dynlex_web_init','_dynlex_web_set_main_source','_dynlex_web_compile_and_emit_wasm','_dynlex_web_get_diagnostics_json','_dynlex_web_get_output_wasm_ptr','_dynlex_web_get_output_wasm_len','_dynlex_web_get_output_wasm_base64','_dynlex_web_get_compiler_log_json','_dynlex_web_get_lsp_hover_json','_dynlex_web_get_lsp_definition_json','_dynlex_web_get_lsp_semantic_tokens_json']"
    )
    set(DYNLEX_WEB_EXPORTED_RUNTIME_METHODS "['ccall','cwrap','UTF8ToString']")
    target_link_options(dynlex_web PRIVATE
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
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/dynlex/lib FILES_MATCHING PATTERN "*.dl")
if(WIN32 AND TARGET ${PROJECT_NAME})
    # Bundle runtime DLLs (LLVM/MSVC deps) so the MSI installation is self-contained.
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION ${CMAKE_INSTALL_BINDIR})

    set(DYNLEX_LLVM_BIN_HINTS "")
    if(DEFINED LLVM_TOOLS_BINARY_DIR AND LLVM_TOOLS_BINARY_DIR)
        list(APPEND DYNLEX_LLVM_BIN_HINTS "${LLVM_TOOLS_BINARY_DIR}")
    endif()
    if(DEFINED LLVM_DIR AND LLVM_DIR)
        get_filename_component(DYNLEX_LLVM_LIB_DIR_FOR_DLLS "${LLVM_DIR}/../.." ABSOLUTE)
        get_filename_component(DYNLEX_LLVM_ROOT_DIR_FOR_DLLS "${DYNLEX_LLVM_LIB_DIR_FOR_DLLS}/.." ABSOLUTE)
        list(APPEND DYNLEX_LLVM_BIN_HINTS "${DYNLEX_LLVM_ROOT_DIR_FOR_DLLS}/bin")
    endif()

    foreach(DYNLEX_REQUIRED_DLL IN ITEMS libc++.dll zlib1.dll)
        unset(DYNLEX_REQUIRED_DLL_PATH CACHE)
        find_file(DYNLEX_REQUIRED_DLL_PATH NAMES "${DYNLEX_REQUIRED_DLL}" HINTS ${DYNLEX_LLVM_BIN_HINTS})
        if(DYNLEX_REQUIRED_DLL_PATH)
            install(FILES "${DYNLEX_REQUIRED_DLL_PATH}" DESTINATION ${CMAKE_INSTALL_BINDIR})
        else()
            message(WARNING "Could not locate required runtime DLL '${DYNLEX_REQUIRED_DLL}' for packaging.")
        endif()
    endforeach()
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
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/johnheikens/DynLex")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, zlib1g, libzstd1")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
include(CPack)
