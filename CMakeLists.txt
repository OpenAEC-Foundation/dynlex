#
# Specify minimum CMake version and project name
#
cmake_minimum_required (VERSION 3.21)
project (dynlex VERSION 0.1.0)

include(GNUInstallDirs)

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

#find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG)

if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)

    if(NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_INCLUDE_DIR}"
        )
    else()
        include(FetchContent)
        FetchContent_Declare(
            nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        )
        FetchContent_MakeAvailable(nlohmann_json)
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
    FILE(GLOB_RECURSE new_list src/*.h)
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
include_directories (src)


#
# Add files
#
file (GLOB_RECURSE PROJECT_HEADERS src/*.hpp
                                   src/*.h)
file (GLOB_RECURSE PROJECT_SOURCES src/*.cpp
                                   src/*.cxx
                                   src/*.cc
                                   src/*.c)
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
add_executable (${PROJECT_NAME} ${PROJECT_SOURCES} ${PROJECT_HEADERS}
                                ${PROJECT_CONFIGS})

#target_include_directories(${PROJECT_NAME}
#PRIVATE  ${Stb_INCLUDE_DIR}
#)
# LLVM 20+ includes the SPIR-V backend as an official target.
# We use the static component libraries for codegen, optimization, native compilation,
# and SPIR-V emission.
llvm_map_components_to_libnames(llvm_libs core support irreader passes native SPIRV)

target_link_libraries (${PROJECT_NAME}
PRIVATE
nlohmann_json::nlohmann_json
${llvm_libs}
)
set_target_properties (${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

#
# Install targets
#
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/dynlex/lib FILES_MATCHING PATTERN "*.dl")

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
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
include(CPack)
