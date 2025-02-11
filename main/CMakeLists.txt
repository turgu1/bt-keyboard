cmake_minimum_required(VERSION 3.16.0)

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)
set(include_dirs ${CMAKE_SOURCE_DIR}/include)

set(CMAKE_CXX_STANDARD "20")
set(SDKCONFIG ${CMAKE_SOURCE_DIR}/build/sdkconfig)

idf_component_register(SRCS "main.cpp" "${app_sources}"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt_keyboard)
