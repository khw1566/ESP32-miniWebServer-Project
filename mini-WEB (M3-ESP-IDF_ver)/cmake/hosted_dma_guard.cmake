# Build a guarded copy. Keep registry downloads and dependency hashes unchanged.
# This prevents the observed pkt_rxbuff assertion from turning transient RX DMA
# pressure into a CPU reset; TCP may retransmit a dropped Wi-Fi packet.
function(m3_hosted_dma_guard)
    idf_component_get_property(hosted_dir espressif__esp_hosted COMPONENT_DIR)
    idf_component_get_property(hosted_target espressif__esp_hosted COMPONENT_LIB)
    file(READ "${hosted_dir}/idf_component.yml" manifest)
    if(NOT manifest MATCHES "version:[ \t]*[\"']?1\\.4\\.1[\"']?([\r\n]|$)")
        message(FATAL_ERROR "M3 DMA guard is scoped to esp_hosted 1.4.1; review newer source before porting")
    endif()
    set(original "${hosted_dir}/host/drivers/transport/sdio/sdio_drv.c")
    file(READ "${original}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    set(anchor "pkt_rxbuff = sdio_buffer_alloc(MEMSET_REQUIRED);\n\t\tassert(pkt_rxbuff);")
    string(FIND "${contents}" "${anchor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Unexpected Hosted SDIO RX allocation; refusing partial DMA guard")
    endif()
    set(replacement [=[/* M3: give the consumer a bounded opportunity to return pooled DMA. */
        pkt_rxbuff = NULL;
        for (unsigned retry = 0; retry < 3 && !pkt_rxbuff; ++retry) {
            pkt_rxbuff = sdio_buffer_alloc(MEMSET_REQUIRED);
            if (!pkt_rxbuff) g_h.funcs->_h_msleep(10);
        }
        if (!pkt_rxbuff) {
            ESP_LOGE(TAG, "M3 SDIO RX DMA exhausted; dropping remaining burst without reboot");
            return ESP_ERR_NO_MEM;
        }]=])
    string(REPLACE "${anchor}" "${replacement}" contents "${contents}")
    set(output_dir "${CMAKE_BINARY_DIR}/m3_hosted_dma_guard")
    file(MAKE_DIRECTORY "${output_dir}")
    set(generated "${output_dir}/sdio_drv.c")
    file(WRITE "${generated}.in" "${contents}")
    configure_file("${generated}.in" "${generated}" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    get_target_property(sources "${hosted_target}" SOURCES)
    set(replacements)
    set(count 0)
    foreach(source IN LISTS sources)
        get_filename_component(basename "${source}" NAME)
        if(basename STREQUAL "sdio_drv.c")
            list(APPEND replacements "${generated}")
            math(EXPR count "${count} + 1")
        else()
            list(APPEND replacements "${source}")
        endif()
    endforeach()
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "M3 DMA guard expected exactly one Hosted SDIO source")
    endif()
    set_property(TARGET "${hosted_target}" PROPERTY SOURCES "${replacements}")
    message(STATUS "M3: Hosted SDIO packet DMA allocation guard enabled")
endfunction()
