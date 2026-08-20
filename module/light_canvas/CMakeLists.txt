
add_library(light_canvas INTERFACE)

target_include_directories(
        light_canvas INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_canvas INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/canvas.c
        ${CMAKE_CURRENT_LIST_DIR}/src/canvas_region.c
)

target_link_libraries(
        light_canvas INTERFACE
        light_display
        light_draw
        light_core
)

# tests are host-only, on the same terms as light_ui's and light_audio's
if(BUILD_TESTING AND LIGHT_PLATFORM STREQUAL HOST)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test light_canvas_test)
endif()
