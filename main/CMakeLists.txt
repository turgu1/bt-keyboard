# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)
set(include_dirs ${CMAKE_SOURCE_DIR}/include)

idf_component_register(SRCS "main.cpp" "bt_keyboard.cpp" "${app_sources}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES esp_hid nvs_flash)
