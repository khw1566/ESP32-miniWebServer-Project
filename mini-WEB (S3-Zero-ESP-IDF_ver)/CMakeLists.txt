cmake_minimum_required(VERSION 3.16)

# Reuse the maintained S3 application while keeping board configuration and
# generated build artifacts independent from the 8MB S3 project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../mini-WEB (S3-ESP-IDF_ver)/main")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini-webserver-s3-zero)

# The connected Zero authenticates reliably with a 15 dBm transmit limit.
# Apply only to this board's shared application component.
idf_component_get_property(s3_zero_app main COMPONENT_LIB)
target_compile_definitions(${s3_zero_app} PRIVATE MINI_WEB_WIFI_TX_POWER_QDBM=60)
