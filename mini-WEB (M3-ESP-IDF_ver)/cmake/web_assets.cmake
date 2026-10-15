function(m3_web_assets)
    idf_build_get_property(asset_python PYTHON)
    set(asset_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${asset_root}/tools/generate_web_gzip.py"
        "${asset_root}/main/web_page_admin.c"
        "${asset_root}/main/web_sd_admin_ui.inc"
        "${asset_root}/main/web_page_ai.c")
    execute_process(COMMAND "${asset_python}" "${asset_root}/tools/generate_web_gzip.py"
        RESULT_VARIABLE asset_result OUTPUT_VARIABLE asset_output ERROR_VARIABLE asset_error)
    if(NOT asset_result EQUAL 0)
        message(FATAL_ERROR "M3 web asset generation failed: ${asset_error}")
    endif()
    string(STRIP "${asset_output}" asset_output)
    message(STATUS "${asset_output}")
endfunction()
