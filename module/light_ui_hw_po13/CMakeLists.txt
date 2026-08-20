
add_library(light_ui_hw_po13 INTERFACE)

target_include_directories(
        light_ui_hw_po13 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_ui_hw_po13 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/hardware.c
)

target_link_libraries(
        light_ui_hw_po13 INTERFACE
        light_display
        light_display_po13
        light_display_sh1107
        light_core
)
