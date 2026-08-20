
add_executable(light_ui_demo_po13)

target_include_directories(
        light_ui_demo_po13 PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# rendered at build time from font-crusher's source truetype font, sized against this
# board's real panel geometry so crush can resolve point size against its physical
# dimensions (64x128 px over 17.2x32.3 mm). 8px, not the 16px face the circle-demo rigs
# render -- see __light_ui_demo_font() for why
crush_add_font_target(light_ui_demo_po13_font
        FONT ${FONT_CRUSHER_PATH}/test_resource/fonts/TypeLightSans.ttf
        DISPLAY_NAME po13 DISPLAY_WIDTH 64 DISPLAY_HEIGHT 128 DISPLAY_DIMENSION 17.2x32.3
        RENDER_NAME light_ui_demo_po13 POINT_SIZE 7 PIXEL_SIZE 8
)

target_sources(
        light_ui_demo_po13 PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/app.c
)

target_link_libraries(
        light_ui_demo_po13 PUBLIC
        light_ui_demo_common
        light_ui_hw_po13
        light_ui_demo_po13_font
        light_button
        light_ui
        light_draw
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        pico_add_extra_outputs(light_ui_demo_po13)
endif()
