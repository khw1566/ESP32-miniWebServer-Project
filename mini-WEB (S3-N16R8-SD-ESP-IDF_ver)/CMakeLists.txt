cmake_minimum_required(VERSION 3.16)
# ESP-IDF prioritizes the environment over a normal CMake variable.
# Refuse an inherited ESP32 selection or compiler cache before configuration.
if(DEFINED ENV{IDF_TARGET} AND NOT "$ENV{IDF_TARGET}" STREQUAL "" AND
   NOT "$ENV{IDF_TARGET}" STREQUAL "esp32s3")
    message(FATAL_ERROR "This N16R8 project requires esp32s3. Set IDF_TARGET=esp32s3 and run: idf.py set-target esp32s3")
endif()
if(DEFINED CACHE{IDF_TARGET} AND NOT "$CACHE{IDF_TARGET}" STREQUAL "esp32s3")
    message(FATAL_ERROR "Build cache belongs to another chip. In this project folder run: idf.py set-target esp32s3")
endif()
set(IDF_TARGET "esp32s3")
set(ENV{IDF_TARGET} "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini-webserver-s3-n16r8-sd)
