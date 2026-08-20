
add_library(light_ui_demo_common INTERFACE)

target_include_directories(
        light_ui_demo_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_ui_demo_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/app.c
)

# no light_touch/light_button here: which input modules exist is a property of the board,
# and each app names its own in its Light_Application_Define
target_link_libraries(
        light_ui_demo_common INTERFACE
        light_canvas
        light_audio
        light_backlight
        light_ui
        light_display
        light_draw
        # the shared command tree (backlight, shutdown, help) and the console feeder live in
        # this module now; whether cli_task actually runs is still each app's choice, via
        # &light_cli in its Light_Application_Define
        light_cli
        light_core
)
