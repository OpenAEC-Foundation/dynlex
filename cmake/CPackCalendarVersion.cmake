# MSI version fields are 8, 8 and 16 bits. Keep the public calendar version in
# every other package format and encode the MSI year as an offset from 2000.
if(CPACK_GENERATOR STREQUAL "WIX")
    if(NOT CPACK_PACKAGE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "Invalid calendar package version: ${CPACK_PACKAGE_VERSION}")
    endif()
    math(EXPR CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1} - 2000")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")
    if(CPACK_PACKAGE_VERSION_MAJOR LESS 0 OR CPACK_PACKAGE_VERSION_MAJOR GREATER 255
        OR CPACK_PACKAGE_VERSION_MINOR LESS 1 OR CPACK_PACKAGE_VERSION_MINOR GREATER 53
        OR CPACK_PACKAGE_VERSION_PATCH GREATER 65535)
        message(FATAL_ERROR "Calendar package version exceeds MSI fields: ${CPACK_PACKAGE_VERSION}")
    endif()
    set(CPACK_PACKAGE_VERSION
        "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()
