
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

# light_canvas owns everything about presenting a frame -- pacing, buffer swapping, dirty
# region flushing -- so light_ui contributes only the widget tree and which parts of it
# changed.
#
# deliberately no light_touch/light_button dependency: light_ui's input entry points are
# hardware-free, so it builds on a host with no device modules present at all (see the
# input section of light_ui.h). applications wire their own devices onto them
target_link_libraries(
        light_ui INTERFACE
        light_canvas
        light_display
        rend
        light_core
)
