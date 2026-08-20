
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
# input section of light_ui.h). applications wire their own devices onto them.
#
# light_cli IS a dependency now, and deliberately so: widgets can carry a command line that
# activation queues for cli_task (see light_ui_set_command_root()), which makes a UI button,
# a console line and a baked boot command three spellings of the same operation. cli is
# hardware-free like light_ui itself, so the host-buildability argument above is untouched
target_link_libraries(
        light_ui INTERFACE
        light_canvas
        light_display
        light_draw
        light_cli
        light_core
)

# tests are host-only: the binary has to run where it is built. BUILD_TESTING is defined by
# include(CTest) in whatever project pulls this module in, so a consumer that never enabled
# testing simply does not get them
if(BUILD_TESTING AND LIGHT_PLATFORM STREQUAL HOST)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test light_ui_test)
endif()
