# esp_audio_codec 2.3.0 adds a raw "-L path" link item. GCC splits that
# unquoted item at spaces on Windows. Its imported archive targets already
# contain full paths, so this redundant search flag can be removed safely.
function(m3_audio_codec_link_compat)
    idf_component_get_property(codec_lib espressif__esp_audio_codec COMPONENT_LIB)
    idf_component_get_property(codec_dir espressif__esp_audio_codec COMPONENT_DIR)
    set(raw_search "-L ${codec_dir}/lib/${IDF_TARGET}")
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(items ${codec_lib} ${property})
        if(items AND NOT items MATCHES "-NOTFOUND$")
            list(REMOVE_ITEM items "${raw_search}" "$<LINK_ONLY:${raw_search}>")
            set_property(TARGET ${codec_lib} PROPERTY ${property} "${items}")
        endif()
    endforeach()
endfunction()
