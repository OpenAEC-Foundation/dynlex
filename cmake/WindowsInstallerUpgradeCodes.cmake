function(dynlex_read_windows_installer_upgrade_codes metadata_path)
    if(NOT EXISTS "${metadata_path}")
        message(FATAL_ERROR
            "Windows installer upgrade code metadata does not exist: ${metadata_path}"
        )
    endif()

    file(STRINGS "${metadata_path}" upgrade_code_records)
    foreach(upgrade_code_record IN LISTS upgrade_code_records)
        if(NOT upgrade_code_record MATCHES "^([a-z]+) ([A-F0-9-]+)$")
            message(FATAL_ERROR
                "Invalid Windows installer upgrade code metadata: ${upgrade_code_record}"
            )
        endif()
        set(upgrade_code_name "${CMAKE_MATCH_1}")
        set(upgrade_code "${CMAKE_MATCH_2}")

        string(LENGTH "${upgrade_code}" upgrade_code_length)
        if(NOT upgrade_code_length EQUAL 36)
            message(FATAL_ERROR
                "Windows installer ${upgrade_code_name} upgrade code must be 36 characters"
            )
        endif()
        foreach(hyphen_offset IN ITEMS 8 13 18 23)
            string(SUBSTRING "${upgrade_code}" ${hyphen_offset} 1 hyphen)
            if(NOT hyphen STREQUAL "-")
                message(FATAL_ERROR
                    "Windows installer ${upgrade_code_name} upgrade code has invalid hyphen placement"
                )
            endif()
        endforeach()
        string(REPLACE "-" "" compact_upgrade_code "${upgrade_code}")
        string(LENGTH "${compact_upgrade_code}" compact_upgrade_code_length)
        if(
            NOT compact_upgrade_code_length EQUAL 32
            OR NOT compact_upgrade_code MATCHES "^[A-F0-9]+$"
        )
            message(FATAL_ERROR
                "Windows installer ${upgrade_code_name} upgrade code is not an uppercase GUID"
            )
        endif()

        string(TOUPPER "${upgrade_code_name}" uppercase_upgrade_code_name)
        set(variable_name
            "DYNLEX_WINDOWS_${uppercase_upgrade_code_name}_UPGRADE_CODE"
        )
        if(DEFINED "${variable_name}")
            message(FATAL_ERROR
                "Duplicate Windows installer ${upgrade_code_name} upgrade code"
            )
        endif()
        set("${variable_name}" "${upgrade_code}")
    endforeach()

    foreach(required_upgrade_code_name IN ITEMS STABLE LEGACY)
        set(variable_name
            "DYNLEX_WINDOWS_${required_upgrade_code_name}_UPGRADE_CODE"
        )
        if(NOT DEFINED "${variable_name}")
            message(FATAL_ERROR
                "Missing Windows installer ${required_upgrade_code_name} upgrade code"
            )
        endif()
        set("${variable_name}" "${${variable_name}}" PARENT_SCOPE)
    endforeach()
endfunction()
