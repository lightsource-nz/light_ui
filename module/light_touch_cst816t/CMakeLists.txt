
add_library(light_touch_cst816t INTERFACE)

target_include_directories(
        light_touch_cst816t INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_touch_cst816t INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/touch_cst816t.c
)

target_link_libraries(
        light_touch_cst816t INTERFACE
        light_touch
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        target_link_libraries(
                light_touch_cst816t INTERFACE
                hardware_gpio
        )
endif()
