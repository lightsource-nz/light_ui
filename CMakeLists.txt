
add_library(light_button INTERFACE)

target_include_directories(
        light_button INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_button INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/button.c
        ${CMAKE_CURRENT_LIST_DIR}/src/button_gpio.c
)

# unlike light_touch/light_display this needs no light_ioport: a button is read as a raw
# GPIO level, not over a transport bus, so there is no io_context to set up
target_link_libraries(
        light_button INTERFACE
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        target_link_libraries(
                light_button INTERFACE
                hardware_gpio
        )
endif()
