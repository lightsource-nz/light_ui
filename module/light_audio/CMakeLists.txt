
add_library(light_audio INTERFACE)

target_include_directories(
        light_audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/audio.c
        ${CMAKE_CURRENT_LIST_DIR}/src/audio_pwm.c
)

# no light_ioport, on the same terms as light_backlight and light_button: a buzzer is driven
# as a PWM output on a pin, not over a transport bus, so there is no io_context to open.
#
# and no hardware_* either any more -- PWM and its DMA streaming live behind light_platform
# now, so this module names no chip peripheral at all and needs no per-platform link block
target_link_libraries(
        light_audio INTERFACE
        light_core
)

# tests are host-only: the binary has to run where it is built. BUILD_TESTING is defined by
# include(CTest) in whatever project pulls this module in, so a consumer that never enabled
# testing simply does not get them.
#
#   AND NOT pico_hostmode: that host flavour declares LIGHT_PLATFORM_HAS_PWM 0 -- it is a host
# build of the Pico SDK, whose PWM peripheral does not exist off-chip -- so the playback
# test's PWM backend has nothing to link (`light_platform_pwm_set_duty` undefined, found the
# moment crossfire's pico_hostmode test tree first consumed this group). The plain HOST_OS
# chip carries PWM stubs and is where this suite has always actually run
if(BUILD_TESTING AND LIGHT_PLATFORM STREQUAL HOST AND NOT LIGHT_CHIP STREQUAL pico_hostmode)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test light_audio_test)
endif()
