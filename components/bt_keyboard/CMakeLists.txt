
cmake_minimum_required(VERSION 3.16.0)

file(GLOB_RECURSE sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${sources} INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src" REQUIRES esp_hid nvs_flash)

project(bt-keyboard)
