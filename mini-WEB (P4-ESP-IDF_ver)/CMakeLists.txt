cmake_minimum_required(VERSION 3.16)
# ESP-IDF prioritizes the environment over a normal CMake variable.
# Refuse an inherited ESP32 selection or compiler cache before configuration.
if(DEFINED ENV{IDF_TARGET} AND NOT "$ENV{IDF_TARGET}" STREQUAL "" AND
   NOT "$ENV{IDF_TARGET}" STREQUAL "esp32p4")
    message(FATAL_ERROR "This P4 project requires esp32p4. Set IDF_TARGET=esp32p4 and run: idf.py set-target esp32p4")
endif()
if(DEFINED CACHE{IDF_TARGET} AND NOT "$CACHE{IDF_TARGET}" STREQUAL "esp32p4")
    message(FATAL_ERROR "Build cache belongs to another chip. In this project folder run: idf.py set-target esp32p4")
endif()
set(IDF_TARGET "esp32p4")
set(ENV{IDF_TARGET} "esp32p4")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini-webserver-p4)
