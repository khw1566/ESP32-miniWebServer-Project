# sdkconfig.defaults does not override an existing sdkconfig. Migrate just these
# M3 runtime settings before project() loads/generates the SDK configuration.
function(m3_memory_policy)
    if(DEFINED SDKCONFIG AND NOT "${SDKCONFIG}" STREQUAL "")
        get_filename_component(config "${SDKCONFIG}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        set(config "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
    endif()
    if(NOT EXISTS "${config}")
        return() # New projects get the same values from sdkconfig.defaults.
    endif()
    file(READ "${config}" original)
    set(updated "${original}")
    foreach(setting IN ITEMS "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0" "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072"
            "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y" "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y"
            "CONFIG_FATFS_API_ENCODING_UTF_8=y" "CONFIG_LV_USE_TILEVIEW=y" "CONFIG_LV_USE_LODEPNG=y"
            "CONFIG_LWIP_TCP_WND_DEFAULT=32768" "CONFIG_LWIP_TCP_RECVMBOX_SIZE=32"
            "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y" "CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y")
        string(REGEX REPLACE "=.*$" "" key "${setting}")
        if(updated MATCHES "(^|\n)# ${key} is not set")
            string(REGEX REPLACE "(^|\n)# ${key} is not set" "\\1${setting}" updated "${updated}")
        elseif(updated MATCHES "(^|\n)${key}=[^\n]*")
            string(REGEX REPLACE "(^|\n)${key}=[^\n]*" "\\1${setting}" updated "${updated}")
        else()
            string(APPEND updated "\n${setting}\n")
        endif()
    endforeach()
    # Bound the new software video decoder's internal IRAM/task footprint even
    # when an older sdkconfig has never seen its component's Kconfig symbols.
    foreach(key IN ITEMS CONFIG_ESP_H264_DECODER_IRAM CONFIG_ESP_H264_DUAL_TASK)
        if(NOT updated MATCHES "(^|\n)(# )?${key}(=| is not set)")
            string(APPEND updated "\n# ${key} is not set\n")
        endif()
    endforeach()
    # Disable alternatives in the TLS allocator choice and the video options.
    foreach(key IN ITEMS CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC
            CONFIG_FATFS_API_ENCODING_ANSI_OEM CONFIG_ESP_H264_DECODER_IRAM CONFIG_ESP_H264_DUAL_TASK
            CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_40 CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360)
        string(REGEX REPLACE "(^|\n)${key}=[^\n]*" "\\1# ${key} is not set" updated "${updated}")
    endforeach()
    if(NOT updated STREQUAL original)
        file(WRITE "${config}" "${updated}")
        message(STATUS "M3: applied PSRAM-first allocation / 128 KiB internal reserve / UTF-8 SD filenames")
    endif()
endfunction()
