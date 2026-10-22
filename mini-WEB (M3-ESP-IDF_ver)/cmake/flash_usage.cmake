# Measure Flash usage by function after every link and store it in the app image.
# The ELF is patched in place before esptool turns it into the .bin, so the
# Settings screen always shows the numbers of the firmware that is running.
function(m3_flash_usage)
    idf_build_get_property(usage_python PYTHON)
    idf_build_get_property(usage_elf EXECUTABLE)
    idf_build_get_property(usage_build BUILD_DIR)
    set(usage_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    add_custom_command(TARGET ${usage_elf} POST_BUILD
        COMMAND "${usage_python}" "${usage_root}/tools/flash_usage.py"
            "${usage_build}/${CMAKE_PROJECT_NAME}.map" --elf "$<TARGET_FILE:${usage_elf}>"
        COMMENT "Measuring Flash usage by function"
        VERBATIM)
endfunction()
