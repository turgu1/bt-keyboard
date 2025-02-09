file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)
set(include_dirs ${CMAKE_SOURCE_DIR}/include)

idf_component_register(SRCS "main.cpp" "${app_sources}"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt_keyboard)
