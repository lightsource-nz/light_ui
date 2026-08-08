
add_library(light_ui INTERFACE)

target_include_directories(
        light_ui INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_ui INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/ui.c
        ${CMAKE_CURRENT_LIST_DIR}/src/ui_draw.c
)

# deliberately no light_touch/light_button dependency -- light_ui's input entry points are
# hardware-free, so it builds on a host with no device modules present at all (see the
# input section of light_ui.h). applications wire their own devices onto them
target_link_libraries(
        light_ui INTERFACE
        light_display
        rend
        light_core
)
