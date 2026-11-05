cmake_minimum_required(VERSION 3.21)

get_filename_component(PROJECT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${PROJECT_DIRECTORY}/cmake/WindowsInstallerUpgradeCodes.cmake")
dynlex_read_windows_installer_upgrade_codes(
    "${PROJECT_DIRECTORY}/metadata/WINDOWS_INSTALLER_UPGRADE_CODES"
)

if(NOT DYNLEX_WINDOWS_STABLE_UPGRADE_CODE STREQUAL
    "F975BC5F-429D-4F99-B362-5014E2B93EE9"
)
    message(FATAL_ERROR "Unexpected stable Windows installer upgrade code")
endif()
if(NOT DYNLEX_WINDOWS_LEGACY_UPGRADE_CODE STREQUAL
    "3FDC26E4-AE29-4FEF-BF7E-9F96E4BE1941"
)
    message(FATAL_ERROR "Unexpected legacy Windows installer upgrade code")
endif()

set(CPACK_GENERATOR WIX)
set(CPACK_PACKAGE_VERSION "2026.45.0")
include("${PROJECT_DIRECTORY}/cmake/CPackCalendarVersion.cmake")
if(NOT CPACK_PACKAGE_VERSION STREQUAL "26.45.0")
    message(FATAL_ERROR "Calendar release was not encoded into MSI version fields")
endif()
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_VERSION "2026.45.0")
include("${PROJECT_DIRECTORY}/cmake/CPackCalendarVersion.cmake")
if(NOT CPACK_PACKAGE_VERSION STREQUAL "2026.45.0")
    message(FATAL_ERROR "Non-MSI packages must retain the public calendar version")
endif()
