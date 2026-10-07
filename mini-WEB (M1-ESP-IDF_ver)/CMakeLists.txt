cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# lwIP allocates IP_NAPT_MAX entries (32 bytes each) only while router mode is on.
# The default of 512 would take 16 KiB of internal RAM; 160 entries take 5 KiB
# and still cover 4 clients browsing ordinary pages.
idf_build_set_property(COMPILE_DEFINITIONS "IP_NAPT_MAX=160" APPEND)
project(mini-webserver-m1)
