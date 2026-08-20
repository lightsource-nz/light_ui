cmake_minimum_required(VERSION 3.18)

#   This file is added two different ways, and it has to work both times -- the same
# arrangement as light_display, which this group follows throughout.
#
#   NORMALLY it is pulled in by a consuming project (screen-test) that has already added the
# framework and the light_display group, so everything these modules link is defined by the
# time we get here and the file reduces to the add_subdirectory() list below.
#
#   STANDALONE (cmake --preset ...) it is the top-level project, and it has to stand up the
# framework, the display group and font-crusher itself. That path is also where the demo
# APPLICATIONS live now: this repository owns the flashable light_ui demos, so its presets and
# scripts are what build and flash them -- screen-test consumes only the libraries.
set(_LIGHT_UI_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(_LIGHT_UI_STANDALONE ON)

        #   LIGHT_PATH is resolved by hand rather than with light_resolve_project(), for the
        # obvious reason: that function lives in the framework, so it cannot be used to find it
        if(DEFINED ENV{LIGHT_PATH} AND (NOT LIGHT_PATH))
                set(LIGHT_PATH "$ENV{LIGHT_PATH}" CACHE PATH "path to the light framework project")
        endif()
        if(DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
                set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "path to the pico-sdk checkout")
        endif()
        if(NOT LIGHT_PATH)
                set(LIGHT_PATH "${CMAKE_CURRENT_LIST_DIR}/../light_framework_mk3" CACHE PATH "path to the light framework project")
        endif()
        get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
        if(NOT EXISTS "${LIGHT_PATH}/light_init.cmake")
                message(FATAL_ERROR
                        "could not find the light framework at '${LIGHT_PATH}'.\n"
                        "Check it out beside this project as ../light_framework_mk3, or set LIGHT_PATH.")
        endif()

        #   FONT_CRUSHER_PATH is resolved here, as a sibling of THIS project's root, for the
        # same reason screen-test resolves it at its own root: light_draw defaults it relative
        # to ITSELF, which only lands when light_display is a proper sibling checkout. CACHE,
        # because light_draw declares it as a cache entry and an uncached value would be
        # silently removed the moment it does.
        if(DEFINED ENV{FONT_CRUSHER_PATH} AND (NOT FONT_CRUSHER_PATH))
                set(FONT_CRUSHER_PATH "$ENV{FONT_CRUSHER_PATH}" CACHE PATH "path to the font-crusher project")
        endif()
        if(NOT FONT_CRUSHER_PATH)
                get_filename_component(_font_crusher_default "${CMAKE_CURRENT_LIST_DIR}/../font-crusher" REALPATH)
                if(EXISTS "${_font_crusher_default}/CMakeLists.txt")
                        set(FONT_CRUSHER_PATH "${_font_crusher_default}" CACHE PATH "path to the font-crusher project")
                endif()
        endif()

        include(${LIGHT_PATH}/external/light_preinit.cmake)

        #   the project and its widget-toolkit module share this name, exactly as light_display
        # and its display-core module do. Harmless -- project() and add_library() are different
        # namespaces -- but it is why the consuming side passes a MARKER to
        # light_resolve_project(): module/light_ui here is what a checkout of the MODULE used
        # to be called, and only the group has the marker
        project(light_ui C CXX ASM)

        # tests are registered by modules that carry them, gated on a host build; enabling at
        # the root is what makes `ctest` in the host tree see them (see light_display's root
        # for the full reasoning)
        include(CTest)

        add_subdirectory(${LIGHT_PATH} light_framework)

        # the display group: light_canvas presents through light_display, light_draw renders
        # the widgets, light_backlight is what the demos dim on idle
        light_resolve_project(LIGHT_DISPLAY light_display MARKER module/light_display/CMakeLists.txt)
        add_subdirectory(${LIGHT_DISPLAY_PATH} light_display_group)
endif()

#   EVERY library module, unconditionally -- they are INTERFACE libraries that compile nothing
# until a target links them, so a consumer that wants only the widget toolkit pays nothing for
# the sensor drivers. Which modules exist is this group's business, no longer a list every
# consumer maintains (screen-test carried eight submodule pointers for these).
add_subdirectory(module/light_ui)
add_subdirectory(module/light_touch)
add_subdirectory(module/light_touch_cst816t)
add_subdirectory(module/light_button)
add_subdirectory(module/light_canvas)
add_subdirectory(module/light_imu)
add_subdirectory(module/light_imu_qmi8658)
add_subdirectory(module/light_audio)
add_subdirectory(module/light_ui_demo_common)

#   the board-wiring modules and the demo applications built on them. The wiring modules are
# added whenever they can compile -- screen-test's own bring-up apps link them from this group
# -- while the demo EXECUTABLES build only standalone: a consumer adding this group wants the
# libraries, not a second copy of every demo image in its build tree.
#
#   the guards themselves follow screen-test's reasoning verbatim: po13 is an EXPANSION board
# (the invariant is the peripheral, any Pico-family host will do), where the touch169 modules
# are for one integrated board and are guarded on exactly its PICO_BOARD -- see the moved
# modules' history in screen-test for the link failure that taught that distinction
if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        add_subdirectory(module/screentest_hw_po13)
        if(_LIGHT_UI_STANDALONE)
                add_subdirectory(module/light_ui_demo_po13)
        endif()

        if(PICO_BOARD STREQUAL waveshare_rp2350_touch_lcd_1.69)
                add_subdirectory(module/screentest_hw_ws_touch169)
                if(_LIGHT_UI_STANDALONE)
                        add_subdirectory(module/light_ui_demo_touch169)
                endif()
        endif()
endif()
